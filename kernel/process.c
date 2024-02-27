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


//0 stand for empty, 1 stand for free a block, 2 stand for assign a block, 3 stand for block merge
block* findx(uint64 mark,uint64 size,uint64 va) 
{
  int i;
  switch(mark){
  case 0:
  {
    for(i=0;i<PGSIZE/sizeof(block)-1;i++)
      if((current->master+i)->mark == mark)
        return current->master+i;
    break;
  }
  case 1:
  {
    for(i=0;i<PGSIZE/sizeof(block)-1;i++)
      if((current->master+i)->mark == mark && (current->master+i)->va==va)
        return current->master+i;
    return current->master-1;
    break;
  }
  case 2:
  {
    for(i=0;i<PGSIZE/sizeof(block)-1;i++)
      if((current->master+i)->mark == mark && (current->master+i)->size>=size)
        return current->master+i;
    return current->master-1;
    break;
  }
  case 3:
  {
    for(i=0;i<PGSIZE/sizeof(block)-1;i++)
      if((current->master+i)->mark == 2 && (current->master+i)->va==va+size)
        return current->master+i;
    return current->master-1;
    break;
  }}
  return current->master-1;
}

block* alloc_block()
{
    void * pa = alloc_page();
    uint64 va = g_ufree_page;
    g_ufree_page += PGSIZE;
    user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,prot_to_type(PROT_WRITE | PROT_READ, 1));
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
  if(size>=4096)
  {
    b = findx(2,0,0);
    uint64 cross = size - b->size;
    b->mark = 1;
    uint64 va = b->va;
    b = findx(2,cross,0);
    if (b==current->master-1) // not found
      b = alloc_block();

    new_b = findx(0,0,0);
    new_b->pa = (uint64)(b->pa);
    new_b->size = size;
    new_b->va = (uint64)(b->va);
    new_b->mark = 1;

    b->size -= size;
    b->va += size;
    b->pa += size;
    return va;
  }
  else 
  {
    b = findx(2,size,0);
    if (b==current->master-1) // not found
      b = alloc_block();

    new_b = findx(0,0,0);
    new_b->pa = (uint64)(b->pa);
    new_b->size = size;
    new_b->va = (uint64)(b->va);
    new_b->mark = 1;

    b->size -= size;
    b->va += size;
    b->pa += size;
    // sprint("malloc: %x\n",new_b->va);
    return new_b->va;
  }
}

void  better_free(uint64 va)
{
  block * b , *p;
  b = findx(1,0,va);
  if (b==current->master-1) panic("Nothing to free!");
  b->mark = 2;
  //merge blocks
  p = findx(3,b->size,va); 
  if (p!=current->master-1)
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
  b->size = current->master->size;
  current->master->size = temp;

  temp = b->pa;
  b->pa = current->master->pa;
  current->master->pa = temp;
  
  temp = b->va;
  b->va = current->master->va;
  current->master->va = temp;
}





//method 2
// uint64 better_alloc(uint64 size)
// {
//   size = ROUNDUP(size,8);

//   uint64 size_of_block = ROUNDUP(sizeof(block),8);
//   block * b=current->free_block,*new_b; //b is the block we want to malloc and p is its pre block
  
//   if(size>PGSIZE) panic("The page try to alloc is to large!\n");

//   if(current->free_block==NULL)
//   {
//     b = alloc_block();
//     sprint("Free_block is NULL, alloc a new_page\n");
//     current->free_block=b;
//   }
//   else if(current->free_block->size>size)
//   {
//     b = current->free_block;
//   }
//   else
//   {
//     while((b->next->size)<size && b->next!=NULL)
//       b=b->next;
//     if(b->next==NULL)
//     {
//       b->next = alloc_block();
//       sprint("No match free_block, alloc a new_page\n");  
//     }
//     b = b->next;
//   }

//   //insert into used_block_chain
//   new_b = current->master+current->num_blocks;
//   current->num_blocks++;
//   new_b->pa = (uint64)(b->pa);
//   new_b->size = size;
//   new_b->va = (uint64)(b->va);
//   new_b->next = current->used_block;
//   current->used_block = new_b;

//   b->size -= size;
//   b->va += size;
//   b->pa += size;
  
//   // sprint ("%x\n",new_b->va);
//   return new_b->va;
// }


// method 1
// void  better_free(uint64 va)
// {
//   block * b=current->used_block, *p; //b is the block we want to free 
//   if(b->va == va)
//   {
//     b=current->used_block;
//     current->used_block->next = current->used_block->next->next;
//   }
//   else
//   {
//     while((b->next->va)!= va && b->next!=NULL)
//       b=b->next;
//     if(b->next==NULL)
//       panic("No block to free!");
//     p=b->next;
//     b->next = p->next;
//     b = p;
//   }

//   //put back
//   if(current->free_block==NULL)
//     current->free_block = b;
//   else{
//     b->next=current->free_block->next;
//     current->free_block = b; 
//   }
// }






















// block* alloc_block()
// {
//     void * pa = alloc_page();
//     uint64 va = g_ufree_page;
//     g_ufree_page += PGSIZE;
//     user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,prot_to_type(PROT_WRITE | PROT_READ, 1));
//     block *b = (block*)pa;
//     //place the block struct in the start of block
//     b->pa = (uint64)pa + ROUNDUP(sizeof(block),8);
//     b->size = PGSIZE - ROUNDUP(sizeof(block),8);
//     b->va = (uint64)va + ROUNDUP(sizeof(block),8);
//     b->next = NULL;
//     return b;
// }

// uint64 better_alloc(uint64 size)
// {
//   size = ROUNDUP(size,8);

//   uint64 size_of_block = ROUNDUP(sizeof(block),8);
//   block * b=current->free_block,*new_b; //b is the block we want to malloc and p is its pre block
  
//   if(size>PGSIZE) panic("The page try to alloc is to large!\n");

//   if(current->free_block==NULL)
//   {
//     b = alloc_block();
//     sprint("Free_block is NULL, alloc a new_page\n");
//     current->free_block=b;
//   }
//   else if(current->free_block->size>size+size_of_block)
//   {
//     b = current->free_block;
//   }
//   else
//   {
//     while((b->next->size)<(size+size_of_block) && b->next!=NULL)
//       b=b->next;
//     if(b->next==NULL)
//     {
//       b->next = alloc_block();
//       sprint("No match free_block, alloc a new_page\n");  
//     }
//     b = b->next;
//   }

//   sprint("%x\n",b);
//   //insert into used_block_chain
//   new_b = (block*)(b->pa);
//   new_b->pa = (uint64)(b->pa) + size_of_block;
//   new_b->size = size;
//   new_b->va = (uint64)(b->va) + size_of_block;
//   new_b->next = current->used_block;
//   current->used_block = new_b;

//   b->size -= (size+size_of_block);
//   b->va += (size+size_of_block);
//   b->pa += (size+size_of_block);
  
//   return new_b->va;
// }

// void  better_free(uint64 va)
// {
//   block * b=current->used_block, *p; //b is the block we want to free 
//   if(b->va == va)
//   {
//     b=current->used_block;
//     current->used_block->next = current->used_block->next->next;
//   }
//   else
//   {
//     while((b->next->va)!= va && b->next!=NULL)
//       b=b->next;
//     if(b->next==NULL)
//       panic("No block to free!");
//     p=b->next;
//     b->next = p->next;
//     b = p;
//   }

//   //put back
//   if(current->free_block==NULL)
//     current->free_block = b;
//   else{
//     b->next=current->free_block->next;
//     current->free_block = b; 
//   }
// }