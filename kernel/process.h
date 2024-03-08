#ifndef _PROC_H_
#define _PROC_H_

#include "riscv.h"
#include "proc_file.h"

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

// riscv-pke kernel supports at most 32 processes
#define NPROC 32
// maximum number of pages in a process's heap
#define MAX_HEAP_PAGES 32

// possible status of a process
enum proc_status {
  FREE,            // unused state
  READY,           // ready state
  RUNNING,         // currently running
  BLOCKED,         // waiting for something
  ZOMBIE,          // terminated but not reclaimed yet
};

// types of a segment
enum segment_type {
  STACK_SEGMENT = 0,   // runtime stack segment
  CONTEXT_SEGMENT, // trapframe segment
  SYSTEM_SEGMENT,  // system segment
  HEAP_SEGMENT,    // runtime heap segment
  CODE_SEGMENT,    // ELF segment
  DATA_SEGMENT,    // ELF segment
};

// the VM regions mapped to a user process
typedef struct mapped_region {
  uint64 va;       // mapped virtual address
  uint32 npages;   // mapping_info is unused if npages == 0
  uint32 seg_type; // segment type, one of the segment_types
} mapped_region;

typedef struct process_heap_manager {
  // points to the last free page in our simple heap.
  uint64 heap_top;
  // points to the bottom of our simple heap.
  uint64 heap_bottom;

  // the address of free pages in the heap
  uint64 free_pages_address[MAX_HEAP_PAGES];
  // the number of free pages in the heap
  uint32 free_pages_count;
}process_heap_manager;

// added lab1c2
// code file struct, including directory index and file name char pointer
typedef struct {
    uint64 dir; 
    char *file;
} code_file;

// address-line number-file name table
typedef struct {
    uint64 addr, line, file;
} addr_line;

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

  // points to a page that contains mapped_regions. below are added @lab3_1
  mapped_region *mapped_info;
  // next free mapped region in mapped_info
  int total_mapped_region;

  // heap management
  process_heap_manager user_heap;

  // process id
  uint64 pid;
  // process status
  int status;
  // parent process
  struct process_t *parent;
  // next queue element
  struct process_t *queue_next;

  // accounting. added @lab3_3
  int tick_count;
  // for wait added @lab3_c1
  uint64 mark;
  struct process_t *block_next;
  // file system. added @lab4_1
  proc_file_management *pfiles;

  // added @lab1_challenge2
  char *debugline; 
  char **dir; 
  code_file *file; 
  addr_line *line; 
  int line_ind;
  block * master;
}process;

// switch to run user app
void switch_to(process*);

// initialize process pool (the procs[] array)
void init_proc_pool();
// allocate an empty process, init its vm space. returns its pid
process* alloc_process();
// reclaim a process, destruct its vm space and free physical pages.
int free_process( process* proc );
// fork a child from parent
int do_fork(process* parent);
ssize_t do_wait(uint64 pid);
// current running process
extern process* current;
void do_exec(char * filename,char * para);
// better malloc
uint64 better_alloc(uint64 size);
void better_free(uint64 va);

// add lab3_c2
typedef struct semphore{
  int signal;
  process *waiting_queue;
}semphore;
#define SEM_MAX 32

long do_sem_new(int resource);
void do_sem_P(int mutex);
void do_sem_V(int mutex);
void insert_to_waiting_queue(int mutex);

#endif
