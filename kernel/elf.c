/*
 * routines that scan and load a (host) Executable and Linkable Format (ELF) file
 * into the (emulated) memory.
 */

#include "elf.h"
#include "string.h"
#include "riscv.h"
#include "vmm.h"
#include "pmm.h"
#include "vfs.h"
#include "syscall.h"
#include "spike_interface/spike_utils.h"

Symbols symbols[64];
int count;
char debug[50000];
uint64 debug_length; 

//
// the implementation of allocater. allocates memory space for later segment loading.
// this allocater is heavily modified @lab2_1, where we do NOT work in bare mode.
//
static void *elf_alloc_mb(elf_ctx *ctx, uint64 elf_pa, uint64 elf_va, uint64 size)
{
  elf_info *msg = (elf_info *)ctx->info;
  // we assume that size of proram segment is smaller than a page.
  kassert(size < 2 * PGSIZE);
  if (size < PGSIZE)
  {
    void *pa = alloc_page();
    if (pa == 0)
      panic("uvmalloc mem alloc falied\n");
    memset((void *)pa, 0, PGSIZE);
    user_vm_map((pagetable_t)msg->p->pagetable, elf_va, PGSIZE, (uint64)pa,
                prot_to_type(PROT_WRITE | PROT_READ | PROT_EXEC, 1));
    return pa;
  }
  else
  {
    void *pa = alloc_two_page();
    if (pa == 0)
      panic("uvmalloc mem alloc falied\n");
    memset((void *)pa, 0, 2 * PGSIZE);
    user_vm_map((pagetable_t)msg->p->pagetable, elf_va, 2 * PGSIZE, (uint64)pa,
                prot_to_type(PROT_WRITE | PROT_READ | PROT_EXEC, 1));

    return pa;
  }
}

//
// actual file reading, using the vfs file interface.
//
static uint64 elf_fpread(elf_ctx *ctx, void *dest, uint64 nb, uint64 offset)
{
  elf_info *msg = (elf_info *)ctx->info;
  vfs_lseek(msg->f, offset, SEEK_SET);
  return vfs_read(msg->f, dest, nb);
}

//
// init elf_ctx, a data structure that loads the elf.
//
elf_status elf_init(elf_ctx *ctx, void *info)
{
  ctx->info = info;

  // load the elf header
  if (elf_fpread(ctx, &ctx->ehdr, sizeof(ctx->ehdr), 0) != sizeof(ctx->ehdr))
    return EL_EIO;

  // check the signature (magic value) of the elf
  if (ctx->ehdr.magic != ELF_MAGIC)
    return EL_NOTELF;

  return EL_OK;
}

void read_uleb128(uint64 *out, char **off) {
    uint64 value = 0; int shift = 0; uint8 b;
    for (;;) {
        b = *(uint8 *)(*off); (*off)++;
        value |= ((uint64)b & 0x7F) << shift;
        shift += 7;
        if ((b & 0x80) == 0) break;
    }
    if (out) *out = value;
}
void read_sleb128(int64 *out, char **off) {
    int64 value = 0; int shift = 0; uint8 b;
    for (;;) {
        b = *(uint8 *)(*off); (*off)++;
        value |= ((uint64_t)b & 0x7F) << shift;
        shift += 7;
        if ((b & 0x80) == 0) break;
    }
    if (shift < 64 && (b & 0x40)) value |= -(1 << shift);
    if (out) *out = value;
}
// Since reading below types through pointer cast requires aligned address,
// so we can only read them byte by byte
void read_uint64(uint64 *out, char **off) {
    *out = 0;
    for (int i = 0; i < 8; i++) {
        *out |= (uint64)(**off) << (i << 3); (*off)++;
    }
}
void read_uint32(uint32 *out, char **off) {
    *out = 0;
    for (int i = 0; i < 4; i++) {
        *out |= (uint32)(**off) << (i << 3); (*off)++;
    }
}
void read_uint16(uint16 *out, char **off) {
    *out = 0;
    for (int i = 0; i < 2; i++) {
        *out |= (uint16)(**off) << (i << 3); (*off)++;
    }
}
void make_addr_line(elf_ctx *ctx, char *debug_line, uint64 length) {
   process *p = ((elf_info *)ctx->info)->p;
   
    p->debugline = debug_line;
    // directory name char pointer array
    p->dir = (char **)((((uint64)debug_line + length + 7) >> 3) << 3); int dir_ind = 0, dir_base;
    // file name char pointer array
    p->file = (code_file *)(p->dir + 64); int file_ind = 0, file_base;
    // table array
    p->line = (addr_line *)(p->file + 64); p->line_ind = 0;
    char *off = debug_line;
    while (off < debug_line + length) { // iterate each compilation unit(CU)
        debug_header *dh = (debug_header *)off; off += sizeof(debug_header);
        dir_base = dir_ind; file_base = file_ind;
        // get directory name char pointer in this CU
        while (*off != 0) {
            p->dir[dir_ind++] = off; while (*off != 0) off++; off++;
        }
        off++;
        // get file name char pointer in this CU
        while (*off != 0) {
            p->file[file_ind].file = off; while (*off != 0) off++; off++;
            uint64 dir; read_uleb128(&dir, &off);
            p->file[file_ind++].dir = dir - 1 + dir_base;
            read_uleb128(NULL, &off); read_uleb128(NULL, &off);
        }
        off++; addr_line regs; regs.addr = 0; regs.file = 1; regs.line = 1;
        // simulate the state machine op code
       
        for (;;) {
            uint8 op = *(off++);
            switch (op) {
                case 0: // Extended Opcodes
                //sprint("0\n");
                    read_uleb128(NULL, &off); op = *(off++);
                    switch (op) {
                        case 1: // DW_LNE_end_sequence
                        //sprint("11\n");
                            if (p->line_ind > 0 && p->line[p->line_ind - 1].addr == regs.addr) p->line_ind--;
                            //sprint("22\n");
                            p->line[p->line_ind] = regs; p->line[p->line_ind].file += file_base - 1;
                            p->line_ind++; goto endop;
                        case 2: // DW_LNE_set_address
                        //sprint("22\n");
                            read_uint64(&regs.addr, &off); break;
                        // ignore DW_LNE_define_file
                        case 4: // DW_LNE_set_discriminator
                            //sprint("44\n");
                            read_uleb128(NULL, &off); break;
                    }
                    break;
                case 1: // DW_LNS_copy
                //sprint("11\n");
                    if (p->line_ind > 0 && p->line[p->line_ind - 1].addr == regs.addr) p->line_ind--;
                    //问题出现在下面这句话 Misaligned Load!
                    //sprint("%d\n",p->line_ind);
                    p->line[p->line_ind] = regs;  
                    //sprint("%d\n",  p->line[p->line_ind].file );
                    p->line[p->line_ind].file += file_base - 1;
                    //sprint("\n");
                    p->line_ind++; break;
                
                case 2: { // DW_LNS_advance_pc
                //sprint("2\n");
                            uint64 delta; read_uleb128(&delta, &off);
                            regs.addr += delta * dh->min_instruction_length;
                            break;
                        }
                case 3: { // DW_LNS_advance_line
                 //sprint("3\n");
                            int64 delta; read_sleb128(&delta, &off);
                            regs.line += delta; break; } case 4: // DW_LNS_set_file
                        read_uleb128(&regs.file, &off); break;
                case 5: // DW_LNS_set_column
                 //sprint("5\n");
                        read_uleb128(NULL, &off); break;
                case 6: // DW_LNS_negate_stmt
                case 7: // DW_LNS_set_basic_block
                        break;
                case 8: { // DW_LNS_const_add_pc
                 //sprint("8\n");
                
                            int adjust = 255 - dh->opcode_base;
                            int delta = (adjust / dh->line_range) * dh->min_instruction_length;
                            regs.addr += delta; break;
                        }
                case 9: { // DW_LNS_fixed_advanced_pc
                //sprint("9\n");
                            uint16 delta; read_uint16(&delta, &off);
                            regs.addr += delta;
                            break;
                        }
                        // ignore 10, 11 and 12
                default: { // Special Opcodes
                //sprint("default\n");
                             int adjust = op - dh->opcode_base;
                             int addr_delta = (adjust / dh->line_range) * dh->min_instruction_length;
                             int line_delta = dh->line_base + (adjust % dh->line_range);
                             regs.addr += addr_delta;
                             regs.line += line_delta;
                             if (p->line_ind > 0 && p->line[p->line_ind - 1].addr == regs.addr) p->line_ind--;
                             p->line[p->line_ind] = regs; p->line[p->line_ind].file += file_base - 1;
                             p->line_ind++; break;
                         }
            }
        } 
      
endop:;
    }
    // for (int i = 0; i < p->line_ind; i++)
    //     sprint("%p %d %d\n", p->line[i].addr, p->line[i].line, p->line[i].file);
}

void load_func_name(elf_ctx *ctx)
{
  // 首先获取sect header来定位str_sh和sys_sh
  elf_sect_header sym_sh;
  elf_sect_header str_sh;
  elf_sect_header shstr_sh;
  elf_sect_header temp_sh;

  // find shstrtab
  uint64 sect_num = ctx->ehdr.shnum;
  uint64 shstr_offset = ctx->ehdr.shoff + ctx->ehdr.shstrndx * sizeof(elf_sect_header);
  elf_fpread(ctx, (void *)&shstr_sh, sizeof(shstr_sh), shstr_offset);
  // sprint("%d\n", shstr_sh.size);   208
  char shstr_str[shstr_sh.size];
  uint64 shstr_sect_off = shstr_sh.offset;
  elf_fpread(ctx, &shstr_str, shstr_sh.size, shstr_sect_off);
  // sprint("%d %d\n", shstr_offset, shstr_sect_off);

  // find strtab and symtab
  for (int i = 0; i < sect_num; i++)
  {
    elf_fpread(ctx, (void *)&temp_sh, sizeof(temp_sh), ctx->ehdr.shoff + i * ctx->ehdr.shentsize);
    uint32 type = temp_sh.type;
    if (strcmp(shstr_str + temp_sh.name, ".symtab") == 0)
      sym_sh = temp_sh;
    else if (strcmp(shstr_str + temp_sh.name, ".strtab") == 0)
      str_sh = temp_sh;
  }

  uint64 str_sect_off = str_sh.offset;
  uint64 sym_num = sym_sh.size / sizeof(elf_sym);

  count = 0;
  for (int i = 0; i < sym_num; i++)
  {
    elf_sym symbol;
    elf_fpread(ctx, (void *)&symbol, sizeof(symbol), sym_sh.offset + i * sizeof(elf_sym));
    if (symbol.name == 0)
      continue;
    if (symbol.info == STT_FUNC + STB_GLOBAL)
    {
      char symname[32];
      elf_fpread(ctx, (void *)&symname, sizeof(symname), str_sect_off + symbol.name); // 里面应该自己有\0
      symbols[count].off = symbol.value;
      strcpy(symbols[count].name, symname);
      // sprint("%s\n",symbols[count].name);
      // sprint("0x%lx\n",symbols[count].off);
      count++;
    }
  }
}

void bubble_sort(Symbols arr[], int n)
{
  for (int i = 0; i < n - 1; i++)
  {
    for (int j = 0; j < n - i - 1; j++)
    {
      if (arr[j].off < arr[j + 1].off)
      {
        // 交换两个元素
        Symbols temp;
        memcpy(&temp, &arr[j], sizeof(Symbols));
        memcpy(&arr[j], &arr[j + 1], sizeof(Symbols));
        memcpy(&arr[j + 1], &temp, sizeof(Symbols));
      }
    }
  }
}

// load debugline
void load_debugline(elf_ctx *ctx)
{
  process *p = ((elf_info *)ctx->info)->p;
  uint64 shr_offset=ctx->ehdr.shoff+ctx->ehdr.shstrndx*sizeof(elf_sect_header); 
  uint64 sect_count=ctx->ehdr.shnum;
  uint64 i=0;
  elf_sect_header tp,shr;
  elf_fpread(ctx,(void *)&shr,sizeof(shr),shr_offset);
  char shr_sy[shr.size];
  elf_fpread(ctx,&shr_sy,shr.size,shr.offset);
  for(i=0;i<sect_count;i++)
  {   
      
      elf_fpread(ctx,(void *)&tp,sizeof(tp),ctx->ehdr.shoff+i*ctx->ehdr.shentsize);
      if(strcmp(shr_sy+tp.name,".debug_line")==0)
      {
        elf_fpread(ctx,(void *)debug,tp.size,tp.offset);
        debug_length=tp.size;
      }
  }
  make_addr_line(ctx,debug,debug_length);
}

//
// load the elf segments to memory regions.
//
elf_status elf_load(elf_ctx *ctx)
{
  // elf_prog_header structure is defined in kernel/elf.h
  elf_prog_header ph_addr;
  int i, off;

  // traverse the elf program segment headers
  for (i = 0, off = ctx->ehdr.phoff; i < ctx->ehdr.phnum; i++, off += sizeof(ph_addr))
  {
    // read segment headers
    if (elf_fpread(ctx, (void *)&ph_addr, sizeof(ph_addr), off) != sizeof(ph_addr))
      return EL_EIO;

    if (ph_addr.type != ELF_PROG_LOAD)
      continue;
    if (ph_addr.memsz < ph_addr.filesz)
      return EL_ERR;
    if (ph_addr.vaddr + ph_addr.memsz < ph_addr.vaddr)
      return EL_ERR;

    // allocate memory block before elf loading
    void *dest = elf_alloc_mb(ctx, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);

    // actual loading
    if (elf_fpread(ctx, dest, ph_addr.memsz, ph_addr.off) != ph_addr.memsz)
      return EL_EIO;

    // record the vm region in proc->mapped_info. added @lab3_1
    int j;
    for (j = 0; j < PGSIZE / sizeof(mapped_region); j++) // seek the last mapped region
      if ((process *)(((elf_info *)(ctx->info))->p)->mapped_info[j].va == 0x0)
        break;

    ((process *)(((elf_info *)(ctx->info))->p))->mapped_info[j].va = ph_addr.vaddr;
    ((process *)(((elf_info *)(ctx->info))->p))->mapped_info[j].npages = 1;

    // SEGMENT_READABLE, SEGMENT_EXECUTABLE, SEGMENT_WRITABLE are defined in kernel/elf.h
    if (ph_addr.flags == (SEGMENT_READABLE | SEGMENT_EXECUTABLE))
    {
      ((process *)(((elf_info *)(ctx->info))->p))->mapped_info[j].seg_type = CODE_SEGMENT;
      // sprint( "CODE_SEGMENT added at mapped info offset:%d\n", j );
    }
    else if (ph_addr.flags == (SEGMENT_READABLE | SEGMENT_WRITABLE))
    {
      ((process *)(((elf_info *)(ctx->info))->p))->mapped_info[j].seg_type = DATA_SEGMENT;
      // sprint( "DATA_SEGMENT added at mapped info offset:%d\n", j );
    }
    else
      panic("unknown program segment encountered, segment flag:%d.\n", ph_addr.flags);

    ((process *)(((elf_info *)(ctx->info))->p))->total_mapped_region++;
  }

  return EL_OK;
}

//
// load the elf of user application, by using the spike file interface.
//
void load_bincode_from_host_elf(process *p, char *filename)
{
  sprint("hartid = %d, Application: %s\n", mycpu(), filename);

  // elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
  elf_ctx elfloader;
  // elf_info is defined above, used to tie the elf file and its corresponding process.
  elf_info info;

  char file_name[256];
  change_path(file_name, filename);
  info.f = vfs_open(file_name, O_RDONLY);
  info.p = p;
  // IS_ERR_VALUE is a macro defined in spike_interface/spike_htif.h
  if (IS_ERR_VALUE(info.f))
    panic("Fail on openning the input application program.\n");

  // init elfloader context. elf_init() is defined above.
  if (elf_init(&elfloader, &info) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf. elf_load() is defined above.
  if (elf_load(&elfloader) != EL_OK)
    panic("Fail on loading elf.\n");

  // added @lab1c1
  if (mycpu() == 0)
  {
    load_func_name(&elfloader);
    bubble_sort(symbols, count);
    load_debugline(&elfloader);
  }

  // entry (virtual, also physical in lab1_x) address
  p->trapframe->epc = elfloader.ehdr.entry;

  // close the vfs file
  vfs_close(info.f);

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}
