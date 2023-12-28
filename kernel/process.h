#ifndef _PROC_H_
#define _PROC_H_

#include "riscv.h"

typedef struct trapframe_t {
  // space to store context (all common registers)
  /* offset:0   */ riscv_regs regs;

  // process's "user kernel" stack
  /* offset:248 */ uint64 kernel_sp;
  // pointer to smode_trap_handler
  /* offset:256 */ uint64 kernel_trap;
  // saved user process counter
  /* offset:264 */ uint64 epc;

  // kernel page table. added @lab2_1
  /* offset:272 */ uint64 kernel_satp;
}trapframe;

typedef struct block_t{
  uint64 size;
  uint64 pa;
  uint64 va;
  // struct block_t * next;
  uint64 mark;   //0 stands for unalloc, 1 stands for use, 2 stands for free
}block;

// the extremely simple definition of process, used for begining labs of PKE
typedef struct process_t {
  // pointing to the stack used in trap handling.
  uint64 kstack;
  // user page table
  pagetable_t pagetable;
  // trapframe storing the context of a (User mode) process.
  trapframe* trapframe;

  // heap master
  // block * used_block;
  // block * free_block;
  block * master;
  // uint64 num_blocks;
}process;




// switch to run user app
void switch_to(process*);
// better malloc
uint64 better_alloc(uint64 size);
void better_free(uint64 va);


// current running process
extern process* current;

// address of the first free page in our simple heap. added @lab2_2
extern uint64 g_ufree_page;

#endif
