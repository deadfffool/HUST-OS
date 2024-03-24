#include "kernel/riscv.h"
#include "kernel/process.h"
#include "spike_interface/spike_utils.h"
#include "util/string.h"
#include "string.h"

static void print_error_line()
{
  uint64 epc=read_csr(mepc);
  addr_line *cur_line=current[mycpu()]->line;
  int i=0;
  
  //find errorline's instruction address
  for(i=0,cur_line=current[mycpu()]->line;i<current[mycpu()]->line_ind&&cur_line->addr!=epc;++i,++cur_line)
    if(cur_line->addr==epc)
      break;

  //find file's path and name
  code_file *cur_file=current[mycpu()]->file+cur_line->file;
  char *file_name=cur_file->file;
  char *file_path=(current[mycpu()]->dir)[cur_file->dir];
  //combine path and name
  
  sprint("Runtime error at %s/%s:%d\n", file_path, file_name, cur_line->line);

  char filename[100];
  int start=strlen(file_path);
  strcpy(filename,file_path);
  filename[start]='/';
  start++;
  strcpy(filename+start,file_name);

  //find error line
  //error instruction's line
  int error_line=cur_line->line;
  //open file
  spike_file_t *file=spike_file_open(filename,O_RDONLY,0);
  //get file's content
  char code_file[1000];
  spike_file_pread(file,(void*)code_file,sizeof(code_file),0);

  //fine error line's start 
  int it=0;
  for(i=1;i<error_line;i++)
  {
    while(code_file[it]!='\n') it++;
    it++;
  }
  char *errorline=code_file+it;
  while(*errorline!='\n') errorline++;
  *errorline='\0';

  sprint("%s\n",code_file+it);
  spike_file_close(file);
}

static void handle_instruction_access_fault() { panic("Instruction access fault!"); }

static void handle_load_access_fault() {
  print_error_line();  
  panic("Load access fault!"); 
}

static void handle_store_access_fault() { panic("Store/AMO access fault!"); }

static void handle_illegal_instruction() { 
  print_error_line();  
  panic("Illegal instruction!"); 
}

static void handle_misaligned_load() { panic("Misaligned Load!"); }

static void handle_misaligned_store() { panic("Misaligned AMO!"); }

// added @lab1_3
static void handle_timer()
{
  int cpuid = 0;
  // setup the timer fired at next time (TIMER_INTERVAL from now)
  *(uint64 *)CLINT_MTIMECMP(cpuid) = *(uint64 *)CLINT_MTIMECMP(cpuid) + TIMER_INTERVAL;

  // setup a soft interrupt in sip (S-mode Interrupt Pending) to be handled in S-mode
  write_csr(sip, SIP_SSIP);
}

//
// handle_mtrap calls a handling function according to the type of a machine mode interrupt (trap).
//
void handle_mtrap()
{
  uint64 mcause = read_csr(mcause);
  switch (mcause)
  {
  case CAUSE_MTIMER:
    handle_timer();
    break;
  case CAUSE_FETCH_ACCESS:
    handle_instruction_access_fault();
    break;
  case CAUSE_LOAD_ACCESS:
    handle_load_access_fault();
  case CAUSE_STORE_ACCESS:
    handle_store_access_fault();
    break;
  case CAUSE_ILLEGAL_INSTRUCTION:
    handle_illegal_instruction();
    break;
  case CAUSE_MISALIGNED_LOAD:
    handle_misaligned_load();
    break;
  case CAUSE_MISALIGNED_STORE:
    handle_misaligned_store();
    break;

  default:
    sprint("machine trap(): unexpected mscause %p\n", mcause);
    sprint("            mepc=%p mtval=%p\n", read_csr(mepc), read_csr(mtval));
    panic("unexpected exception happened in M-mode.\n");
    break;
  }
}
