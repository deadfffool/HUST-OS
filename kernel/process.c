/*
 * Utility functions for process management. 
 *
 * Note: in Lab1, only one process (i.e., our user application) exists. Therefore, 
 * PKE OS at this stage will set "current" to the loaded user application, and also
 * switch to the old "current" process after trap handling.
 */

#include "riscv.h"
#include "strap.h"
#include "config.h"
#include "process.h"
#include "elf.h"
#include "string.h"
#include "vmm.h"
#include "pmm.h"
#include "memlayout.h"
#include "sched.h"
#include "spike_interface/spike_utils.h"
#include "elf.h"
#include "sync.h"
#include "util/string.h"
#include "util/functions.h"

//Two functions defined in kernel/usertrap.S
extern char smode_trap_vector[];
extern void return_to_user(trapframe *, uint64 satp);

// trap_sec_start points to the beginning of S-mode trap segment (i.e., the entry point
// of S-mode trap vector).
extern char trap_sec_start[];

// process pool. added @lab3_1
process procs[NPROC];
process temp;

spinlock procs_lock;

// current points to the currently running user-mode application.
process* current[NCPU];

int cur=0;
semphore signal[SEM_MAX];
//
// switch to a user-mode process
//
void switch_to(process* proc) {
  assert(proc);
  current[mycpu()] = proc;
  // write the smode_trap_vector (64-bit func. address) defined in kernel/strap_vector.S
  // to the stvec privilege register, such that trap handler pointed by smode_trap_vector
  // will be triggered when an interrupt occurs in S mode.
  write_csr(stvec, (uint64)smode_trap_vector);

  // set up trapframe values (in process structure) that smode_trap_vector will need when
  // the process next re-enters the kernel.
  proc->trapframe->kernel_sp = proc->kstack;      // process's kernel stack
  proc->trapframe->kernel_satp = read_csr(satp);  // kernel page table
  proc->trapframe->kernel_trap = (uint64)smode_trap_handler;
  proc->trapframe->regs.tp = mycpu();

  // SSTATUS_SPP and SSTATUS_SPIE are defined in kernel/riscv.h
  // set S Previous Privilege mode (the SSTATUS_SPP bit in sstatus register) to User mode.
  unsigned long x = read_csr(sstatus);
  x &= ~SSTATUS_SPP;  // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE;  // enable interrupts in user mode

  // write x back to 'sstatus' register to enable interrupts, and sret destination mode.
  write_csr(sstatus, x);

  // set S Exception Program Counter (sepc register) to the elf entry pc.
  write_csr(sepc, proc->trapframe->epc);

  // make user page table. macro MAKE_SATP is defined in kernel/riscv.h. added @lab2_1
  uint64 user_satp = MAKE_SATP(proc->pagetable);

  // return_to_user() is defined in kernel/strap_vector.S. switch to user mode with sret.
  // note, return_to_user takes two parameters @ and after lab2_1.
  return_to_user(proc->trapframe, user_satp);
}

//
// initialize process pool (the procs[] array). added @lab3_1
//
void init_proc_pool() {
  memset( procs, 0, sizeof(process)*NPROC );

  for (int i = 0; i < NPROC; ++i) {
    procs[i].status = FREE;
    procs[i].pid = i;
  }
}

//
// allocate an empty process, init its vm space. returns the pointer to
// process strcuture. added @lab3_1
//
process *alloc_process()
{
  // locate the first usable process structure
  acquire(&procs_lock);
  int i;
  for (i = 0; i < NPROC; i++)
    if (procs[i].status == FREE)
      break;

  if (i >= NPROC)
  {
    panic("cannot find any free process structure.\n");
    return 0;
  }

  // init proc[i]'s vm space
  procs[i].trapframe = (trapframe *)alloc_page(); // trapframe, used to save context
  memset(procs[i].trapframe, 0, sizeof(trapframe));

  // page directory
  procs[i].pagetable = (pagetable_t)alloc_page();
  memset((void *)procs[i].pagetable, 0, PGSIZE);

  procs[i].kstack = (uint64)alloc_page() + PGSIZE; // user kernel stack top
  uint64 user_stack = (uint64)alloc_page();        // phisical address of user stack bottom
  procs[i].trapframe->regs.sp = USER_STACK_TOP;    // virtual address of user stack top

  // allocates a page to record memory regions (segments)
  procs[i].mapped_info = (mapped_region *)alloc_page();
  memset(procs[i].mapped_info, 0, PGSIZE);

  // map user stack in userspace
  user_vm_map((pagetable_t)procs[i].pagetable, USER_STACK_TOP - PGSIZE, PGSIZE,
              user_stack, prot_to_type(PROT_WRITE | PROT_READ, 1));
  procs[i].mapped_info[STACK_SEGMENT].va = USER_STACK_TOP - PGSIZE;
  procs[i].mapped_info[STACK_SEGMENT].npages = 1;
  procs[i].mapped_info[STACK_SEGMENT].seg_type = STACK_SEGMENT;

  // map trapframe in user space (direct mapping as in kernel space).
  user_vm_map((pagetable_t)procs[i].pagetable, (uint64)procs[i].trapframe, PGSIZE,
              (uint64)procs[i].trapframe, prot_to_type(PROT_WRITE | PROT_READ, 0));
  procs[i].mapped_info[CONTEXT_SEGMENT].va = (uint64)procs[i].trapframe;
  procs[i].mapped_info[CONTEXT_SEGMENT].npages = 1;
  procs[i].mapped_info[CONTEXT_SEGMENT].seg_type = CONTEXT_SEGMENT;

  // map S-mode trap vector section in user space (direct mapping as in kernel space)
  // we assume that the size of usertrap.S is smaller than a page.
  user_vm_map((pagetable_t)procs[i].pagetable, (uint64)trap_sec_start, PGSIZE,
              (uint64)trap_sec_start, prot_to_type(PROT_READ | PROT_EXEC, 0));
  procs[i].mapped_info[SYSTEM_SEGMENT].va = (uint64)trap_sec_start;
  procs[i].mapped_info[SYSTEM_SEGMENT].npages = 1;
  procs[i].mapped_info[SYSTEM_SEGMENT].seg_type = SYSTEM_SEGMENT;

  // sprint("in alloc_proc. user frame 0x%lx, user stack 0x%lx, user kstack 0x%lx \n",
  //        procs[i].trapframe, procs[i].trapframe->regs.sp, procs[i].kstack);

  // initialize the process's heap manager
  procs[i].user_heap.heap_top = USER_FREE_ADDRESS_START;
  procs[i].user_heap.heap_bottom = USER_FREE_ADDRESS_START;
  procs[i].user_heap.free_pages_count = 0;

  // map user heap in userspace
  procs[i].mapped_info[HEAP_SEGMENT].va = USER_FREE_ADDRESS_START;
  procs[i].mapped_info[HEAP_SEGMENT].npages = 0; // no pages are mapped to heap yet.
  procs[i].mapped_info[HEAP_SEGMENT].seg_type = HEAP_SEGMENT;

  procs[i].total_mapped_region = 4;

  // initialize files_struct
  procs[i].pfiles = init_proc_file_management();
  // sprint("in alloc_proc. build proc_file_management successfully.\n");
  procs[i].status = USED;
  // return after initialization.
  release(&procs_lock);
  return &procs[i];
}


//
// reclaim a process. added @lab3_1
//
int free_process( process* proc ) {
  // we set the status to ZOMBIE, but cannot destruct its vm space immediately.
  // since proc can be current process, and its user kernel stack is currently in use!
  // but for proxy kernel, it (memory leaking) may NOT be a really serious issue,
  // as it is different from regular OS, which needs to run 7x24.
  proc->status = ZOMBIE;

  return 0;
}

//
// implements fork syscal in kernel. added @lab3_1
// basic idea here is to first allocate an empty process (child), then duplicate the
// context and data segments of parent process to the child, and lastly, map other
// segments (code, system) of the parent to child. the stack segment remains unchanged
// for the child.
//
int do_fork(process *parent)
{
  // sprint("will fork a child from parent %d.\n", parent->pid);
  process *child = alloc_process();

  for (int i = 0; i < parent->total_mapped_region; i++)
  {
    // browse parent's vm space, and copy its trapframe and data segments,
    // map its code segment.
    switch (parent->mapped_info[i].seg_type)
    {
    case CONTEXT_SEGMENT:
    {
      *child->trapframe = *parent->trapframe;
      break;
    }
    case STACK_SEGMENT:
    {
      memcpy((void *)lookup_pa(child->pagetable, child->mapped_info[STACK_SEGMENT].va),
             (void *)lookup_pa(parent->pagetable, parent->mapped_info[i].va), PGSIZE);
      break;
    }
    case HEAP_SEGMENT:
      // build a same heap for child process.

      // convert free_pages_address into a filter to skip reclaimed blocks in the heap
      // when mapping the heap blocks
      {
        int free_block_filter[MAX_HEAP_PAGES];
        memset(free_block_filter, 0, MAX_HEAP_PAGES);
        uint64 heap_bottom = parent->user_heap.heap_bottom;
        for (int i = 0; i < parent->user_heap.free_pages_count; i++)
        {
          int index = (parent->user_heap.free_pages_address[i] - heap_bottom) / PGSIZE;
          free_block_filter[index] = 1;
        }

        // copy and map the heap blocks
        for (uint64 heap_block = current[mycpu()]->user_heap.heap_bottom;
             heap_block < current[mycpu()]->user_heap.heap_top; heap_block += PGSIZE)
        {
          if (free_block_filter[(heap_block - heap_bottom) / PGSIZE]) // skip free blocks
            continue;

          // COW: just map (not cp) heap here
          uint64 child_pa = lookup_pa(parent->pagetable, heap_block);
          user_vm_map((pagetable_t)child->pagetable, heap_block, PGSIZE, child_pa, prot_to_type(PROT_READ | PROT_COW, 1));
        }
      }

      child->mapped_info[HEAP_SEGMENT].npages = parent->mapped_info[HEAP_SEGMENT].npages;

      // copy the heap manager from parent to child
      memcpy((void *)&child->user_heap, (void *)&parent->user_heap, sizeof(parent->user_heap));
      break;
    case CODE_SEGMENT:
      {
        uint64 va = parent->mapped_info[i].va, size = parent->mapped_info[i].npages * PGSIZE, pa = lookup_pa(parent->pagetable, parent->mapped_info[i].va);
        int perm = prot_to_type(PROT_EXEC | PROT_READ, 1);
        map_pages(child->pagetable, va, size, pa, perm);

        // after mapping, register the vm region (do not delete codes below!)
        child->mapped_info[child->total_mapped_region].va = parent->mapped_info[i].va;
        child->mapped_info[child->total_mapped_region].npages =
            parent->mapped_info[i].npages;
        child->mapped_info[child->total_mapped_region].seg_type = CODE_SEGMENT;
        child->total_mapped_region++;
        break;
      }
    case DATA_SEGMENT:
        {
          for( int j=0; j<parent->mapped_info[i].npages; j++ ){
            uint64 addr = lookup_pa(parent->pagetable, parent->mapped_info[i].va+j*PGSIZE);
            char *newaddr = alloc_page(); memcpy(newaddr, (void *)addr, PGSIZE);
            map_pages(child->pagetable, parent->mapped_info[i].va+j*PGSIZE, PGSIZE,
                    (uint64)newaddr, prot_to_type(PROT_WRITE | PROT_READ, 1));
        }

        // after mapping, register the vm region (do not delete codes below!)
        child->mapped_info[child->total_mapped_region].va = parent->mapped_info[i].va;
        child->mapped_info[child->total_mapped_region].npages = 
          parent->mapped_info[i].npages;
        child->mapped_info[child->total_mapped_region].seg_type = DATA_SEGMENT;
        child->total_mapped_region++;
        break;
        }
    }
  }

  child->status = READY;
  child->trapframe->regs.a0 = 0;
  child->parent = parent;
  insert_to_ready_queue(child);

  return child->pid;
}

//add_@lab4_c2
process* alloc_process_without_sprint() {
  // locate the first usable process structure
  // init proc[i]'s vm space
  temp.trapframe = (trapframe *)alloc_page();  //trapframe, used to save context
  memset(temp.trapframe, 0, sizeof(trapframe));

  // page directory
  temp.pagetable = (pagetable_t)alloc_page();
  memset((void *)temp.pagetable, 0, PGSIZE);

  temp.kstack = (uint64)alloc_page() + PGSIZE;   //user kernel stack top
  uint64 user_stack = (uint64)alloc_page();       //phisical address of user stack bottom
  temp.trapframe->regs.sp = USER_STACK_TOP;  //virtual address of user stack top

  // allocates a page to record memory regions (segments)
  temp.mapped_info = (mapped_region*)alloc_page();
  memset( temp.mapped_info, 0, PGSIZE );

  // map user stack in userspace
  user_vm_map((pagetable_t)temp.pagetable, USER_STACK_TOP - PGSIZE, PGSIZE,
    user_stack, prot_to_type(PROT_WRITE | PROT_READ, 1));
  temp.mapped_info[STACK_SEGMENT].va = USER_STACK_TOP - PGSIZE;
  temp.mapped_info[STACK_SEGMENT].npages = 1;
  temp.mapped_info[STACK_SEGMENT].seg_type = STACK_SEGMENT;

  // map trapframe in user space (direct mapping as in kernel space).
  user_vm_map((pagetable_t)temp.pagetable, (uint64)temp.trapframe, PGSIZE,
    (uint64)temp.trapframe, prot_to_type(PROT_WRITE | PROT_READ, 0));
  temp.mapped_info[CONTEXT_SEGMENT].va = (uint64)temp.trapframe;
  temp.mapped_info[CONTEXT_SEGMENT].npages = 1;
  temp.mapped_info[CONTEXT_SEGMENT].seg_type = CONTEXT_SEGMENT;

  // map S-mode trap vector section in user space (direct mapping as in kernel space)
  // we assume that the size of usertrap.S is smaller than a page.
  user_vm_map((pagetable_t)temp.pagetable, (uint64)trap_sec_start, PGSIZE,
    (uint64)trap_sec_start, prot_to_type(PROT_READ | PROT_EXEC, 0));
  temp.mapped_info[SYSTEM_SEGMENT].va = (uint64)trap_sec_start;
  temp.mapped_info[SYSTEM_SEGMENT].npages = 1;
  temp.mapped_info[SYSTEM_SEGMENT].seg_type = SYSTEM_SEGMENT;

  // initialize the process's heap manager
  temp.user_heap.heap_top = USER_FREE_ADDRESS_START;
  temp.user_heap.heap_bottom = USER_FREE_ADDRESS_START;
  temp.user_heap.free_pages_count = 0;

  // map user heap in userspace
  temp.mapped_info[HEAP_SEGMENT].va = USER_FREE_ADDRESS_START;
  temp.mapped_info[HEAP_SEGMENT].npages = 0;  // no pages are mapped to heap yet.
  temp.mapped_info[HEAP_SEGMENT].seg_type = HEAP_SEGMENT;

  temp.total_mapped_region = 4;

  // initialize files_struct
  temp.pfiles = init_proc_file_management();
  // return after initialization.
  return &temp;
}

//add_@lab4_c2
void do_exec(char * filename,char * argv)
{  
  elf_info info;
  process * p = alloc_process_without_sprint();
  load_bincode_from_host_elf(p,filename);
  
  current[mycpu()]->kstack = p->kstack;
  current[mycpu()]->pagetable = p->pagetable;
  current[mycpu()]->trapframe = p->trapframe;
  current[mycpu()]->total_mapped_region = p->total_mapped_region;
  current[mycpu()]->mapped_info = p->mapped_info;
  current[mycpu()]->user_heap = p->user_heap;
  current[mycpu()]->parent = p->parent;
  current[mycpu()]->queue_next = p->queue_next;
  current[mycpu()]->tick_count = p->tick_count;
  current[mycpu()]->pfiles = p->pfiles;
  
  // better malloc
  void * pa = alloc_page();
  uint64 va = current[mycpu()]->user_heap.heap_top;
  current[mycpu()]->user_heap.heap_top += PGSIZE;
  current[mycpu()]->mapped_info[HEAP_SEGMENT].npages++;
  user_vm_map((pagetable_t)current[mycpu()]->pagetable, va, PGSIZE, (uint64)pa,prot_to_type(PROT_WRITE | PROT_READ, 1));
  current[mycpu()]->master = (block*)pa;
  memset(pa,0,PGSIZE);
  
  // args
  size_t * vsp, * sp;
  vsp = (size_t *)current[mycpu()]->trapframe->regs.sp;
  vsp -= 8;
  sp = (size_t *)user_va_to_pa(current[mycpu()]->pagetable, (void*)vsp);
  memcpy((char *)sp, argv, 1+strlen(argv));
  vsp--;sp--;
  * sp = (uint64)(1+vsp);

  current[mycpu()]->trapframe->regs.sp = (uint64)vsp;
  current[mycpu()]->trapframe->regs.a1 = (uint64)vsp;
  current[mycpu()]->trapframe->regs.a0 = (uint64)1;

  switch_to(current[mycpu()]);
}

ssize_t do_wait(uint64 pid)
{
  int flag = 0;
  uint64 child_pid;
  if (pid == -1)
  {
    //search
    for(int i = 0;i<NPROC;i++)
    {
      if (procs[i].parent == current[mycpu()])
      {
        flag = 1 ;
        child_pid = procs[i].pid;
        if (procs[i].status == ZOMBIE)
        {
          procs[i].status = FREE;
          return i;
        }
      }
    }
    // if it is running 
    if(flag == 0) return -1;
    else 
    {
      current[mycpu()]->mark=child_pid;
      insert_to_blocked_queue(current[mycpu()]);
      schedule();
      return -2;
    }
  }
  else
  {     
    for(int i = 0;i<NPROC;i++)
    {
      if (procs[i].pid == pid)
      {
        flag = 1;
        child_pid = procs[i].pid;
        if (procs[pid].status == ZOMBIE) 
        {
          procs[pid].status = FREE;
          return pid;
        }
      }
    }
    if(flag == 0) return -1;
    else
    {
      current[mycpu()]->mark = child_pid;
      insert_to_blocked_queue(current[mycpu()]);
      schedule();
      return -2;
    }
  }
}

//0 stand for empty, 1 stand for free a block, 2 stand for assign a block, 3 stand for block merge
block* findx(uint64 mark,uint64 size,uint64 va) 
{
  int i;
  switch(mark){
  case 0:
  {
    for(i=0;i<PGSIZE/sizeof(block)-1;i++)
      if((current[mycpu()]->master+i)->mark == mark)
        return current[mycpu()]->master+i;
    break;
  }
  case 1:
  {
    for(i=0;i<PGSIZE/sizeof(block)-1;i++)
      if((current[mycpu()]->master+i)->mark == mark && (current[mycpu()]->master+i)->va==va)
        return current[mycpu()]->master+i;
    return current[mycpu()]->master-1;
    break;
  }
  case 2:
  {
    for(i=0;i<PGSIZE/sizeof(block)-1;i++)
      if((current[mycpu()]->master+i)->mark == mark && (current[mycpu()]->master+i)->size>=size)
        return current[mycpu()]->master+i;
    return current[mycpu()]->master-1;
    break;
  }
  case 3:
  {
    for(i=0;i<PGSIZE/sizeof(block)-1;i++)
      if((current[mycpu()]->master+i)->mark == 2 && (current[mycpu()]->master+i)->va==va+size)
        return current[mycpu()]->master+i;
    return current[mycpu()]->master-1;
    break;
  }}
  return current[mycpu()]->master-1;
}

block* alloc_block()
{
    void * pa = alloc_page();
    uint64 va = current[mycpu()]->user_heap.heap_top;
    current[mycpu()]->user_heap.heap_top += PGSIZE;
    current[mycpu()]->mapped_info[HEAP_SEGMENT].npages++;
    user_vm_map((pagetable_t)current[mycpu()]->pagetable, va, PGSIZE, (uint64)pa,prot_to_type(PROT_WRITE | PROT_READ, 1));
    //place the block struct in master
    block *b = findx(0,0,0);
    
    b->pa = (uint64)pa;
    b->size = PGSIZE;
    b->va = (uint64)va;
    b->mark = 2;
    return b;
}

uint64 better_alloc(uint64 size)
{
  size = ROUNDUP(size,8);

  uint64 size_of_block = ROUNDUP(sizeof(block),8);
  block *b,*new_b; //b is the block we want to malloc
  
  b = findx(2,size,0);
  if (b==current[mycpu()]->master-1) // not found
    b = alloc_block();
  
  new_b = findx(0,0,0);
  new_b->pa = (uint64)(b->pa);
  new_b->size = size;
  new_b->va = (uint64)(b->va);
  new_b->mark = 1;

  b->size -= size;
  b->va += size;
  b->pa += size;
  sprint("malloc: %x\n",new_b->va);
  return new_b->va;
}

void  better_free(uint64 va)
{
  block * b , *p;
  b = findx(1,0,va);
  if (b==current[mycpu()]->master-1) 
    panic("Nothing to free!");
  b->mark = 2;
  //merge blocks
  p = findx(3,b->size,va); 
  if (p!=current[mycpu()]->master-1)
  {
    p->va -= b->size;
    p->pa -= b->size;
    p->size += b->size;
    b->mark = 0;
    return;
  } 
  //put it in first
  uint64 temp;
  temp = b->size;
  b->size = current[mycpu()]->master->size;
  current[mycpu()]->master->size = temp;

  temp = b->pa;
  b->pa = current[mycpu()]->master->pa;
  current[mycpu()]->master->pa = temp;
  
  temp = b->va;
  b->va = current[mycpu()]->master->va;
  current[mycpu()]->master->va = temp;
}


//PV operation function
long do_sem_new(int resource)
{
  if(cur<SEM_MAX)
  {
    signal[cur].signal = resource;
    return cur++;
  }
  panic("Too many signals!You should notice your process.\n");
  return -1;
}

//P operation
void do_sem_P(int mutex){
  if(mutex<0||mutex>=SEM_MAX)
  {
    panic("Your signal is error!\n");
    return;
  }
  signal[mutex].signal--;
  if(signal[mutex].signal<0){
    //insert current process to this signal's waiting list
    insert_to_waiting_queue(mutex);
    //schedule a ready process
    schedule();
  }
  
}

//V operation
void do_sem_V(int mutex){
  if(mutex<0||mutex>=SEM_MAX)
  {
    panic("Your signal is error!\n");
    return;
  }
  signal[mutex].signal++;
  if(signal[mutex].signal<=0)
  {
    if(signal[mutex].waiting_queue!=NULL){
      process* cur=signal[mutex].waiting_queue;
      signal[mutex].waiting_queue=signal[mutex].waiting_queue->queue_next;
      cur->status = READY;
      insert_to_ready_queue(cur);
    }
  }
  
}

//P operation's insert
void insert_to_waiting_queue(int mutex){
  if(signal[mutex].waiting_queue==NULL)
  {
    signal[mutex].waiting_queue = current[mycpu()];
  }
  else{
    process *cur=signal[mutex].waiting_queue;
    for(;cur->queue_next!=NULL;cur=cur->queue_next)
    {
      if(cur==current[mycpu()]) return;
    }
    if(cur==current[mycpu()]) return;
    cur->queue_next=current[mycpu()];
  }
  
  current[mycpu()]->queue_next=NULL;
  current[mycpu()]->status=BLOCKED;
}