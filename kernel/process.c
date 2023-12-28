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
#include "util/functions.h"
#include "spike_interface/spike_utils.h"

//Two functions defined in kernel/usertrap.S
extern char smode_trap_vector[];
extern void return_to_user(trapframe *, uint64 satp);

// current points to the currently running user-mode application.
process* current = NULL;

// points to the first free page in our simple heap. added @lab2_2
uint64 g_ufree_page = USER_FREE_ADDRESS_START;

//
// switch to a user-mode process
//
void switch_to(process* proc) {
  assert(proc);
  current = proc;

  // write the smode_trap_vector (64-bit func. address) defined in kernel/strap_vector.S
  // to the stvec privilege register, such that trap handler pointed by smode_trap_vector
  // will be triggered when an interrupt occurs in S mode.
  write_csr(stvec, (uint64)smode_trap_vector);

  // set up trapframe values (in process structure) that smode_trap_vector will need when
  // the process next re-enters the kernel.
  proc->trapframe->kernel_sp = proc->kstack;      // process's kernel stack
  proc->trapframe->kernel_satp = read_csr(satp);  // kernel page table
  proc->trapframe->kernel_trap = (uint64)smode_trap_handler;

  //better malloc
  proc->free_block = NULL;
  proc->used_block = NULL;

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

block* alloc_block()
{
    void * pa = alloc_page();
    uint64 va = g_ufree_page;
    g_ufree_page += PGSIZE;
    user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,prot_to_type(PROT_WRITE | PROT_READ, 1));
    block *b = (block*)pa;
    //place the block struct in the start of block
    b->pa = (uint64)pa + ROUNDUP(sizeof(block),8);
    b->size = PGSIZE- ROUNDUP(sizeof(block),8);
    b->va = (uint64)va + ROUNDUP(sizeof(block),8);
    b->next = NULL;
    return b;
}

uint64 better_alloc(uint64 size)
{
  size = ROUNDUP(size,8);

  uint64 re_va,size_of_block = ROUNDUP(sizeof(block),8);
  block * b=current->free_block,* p=current->free_block,*new_b; //b is the block we want to malloc and p is its pre block
  if(size>PGSIZE) panic("The page try to alloc is to large!\n");

  if(current->free_block!=NULL)
  {
    b=b->next;
    while((b->size+size_of_block)<size && b!=NULL)
    {
      b=b->next;
      p=p->next;
    }
    if(b==NULL)
    {
      b = alloc_block();
      p->next = b;
    }
  }
  else
  {
    b = alloc_block();
    current->free_block=b;
  }


  new_b = (block*)b->va;
  new_b->pa = (uint64)b->pa + size_of_block;
  new_b->size = size;
  new_b->va = (uint64)b->va + size_of_block;
  new_b->next = NULL;

  b->size -= size;
  b->va += size;
  b->pa += size;
  
  

  return re_va;
}

void  better_free(uint64 va)
{

}