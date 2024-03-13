/*
 * Supervisor-mode startup codes
 */

#include "riscv.h"
#include "string.h"
#include "elf.h"
#include "process.h"
#include "pmm.h"
#include "vmm.h"
#include "sched.h"
#include "memlayout.h"
#include "spike_interface/spike_utils.h"
#include "util/types.h"
#include "vfs.h"
#include "rfs.h"
#include "ramdev.h"
#include "sync.h"

//
// trap_sec_start points to the beginning of S-mode trap segment (i.e., the entry point of
// S-mode trap vector). added @lab2_1
//
extern char trap_sec_start[];

// Synchronize
extern void sync_barrier(volatile int *counter, int all);
volatile int barrier = 0;

//
// turn on paging. added @lab2_1
//
void enable_paging() {
  // write the pointer to kernel page (table) directory into the CSR of "satp".
  write_csr(satp, MAKE_SATP(g_kernel_pagetable));

  // refresh tlb to invalidate its content.
  flush_tlb();
}

typedef union {
  uint64 buf[MAX_CMDLINE_ARGS];
  char *argv[MAX_CMDLINE_ARGS];
} arg_buf;

//
// returns the number (should be 1) of string(s) after PKE kernel in command line.
// and store the string(s) in arg_bug_msg.
//
static size_t parse_args(arg_buf *arg_bug_msg) {
  // HTIFSYS_getmainvars frontend call reads command arguments to (input) *arg_bug_msg
  long r = frontend_syscall(HTIFSYS_getmainvars, (uint64)arg_bug_msg,
      sizeof(*arg_bug_msg), 0, 0, 0, 0, 0);
  kassert(r == 0);

  size_t pk_argc = arg_bug_msg->buf[0];
  uint64 *pk_argv = &arg_bug_msg->buf[1];

  int arg = 1;  // skip the PKE OS kernel string, leave behind only the application name
  for (size_t i = 0; arg + i < pk_argc; i++)
    arg_bug_msg->argv[i] = (char *)(uintptr_t)pk_argv[arg + i];

  //returns the number of strings after PKE kernel in command line
  return pk_argc - arg;
}

//
// load the elf, and construct a "process" (with only a trapframe).
// load_bincode_from_host_elf is defined in elf.c
//
process * load_user_program() { 
  // alloc_page();alloc_page();alloc_page();alloc_page();alloc_page();alloc_page();alloc_page();alloc_page();
  // allocate a page to store the trapframe. alloc_page is defined in kernel/pmm.c. added @lab2_1
  process * proc = alloc_process();
  sprint("hartid = %d, User application is loading.\n", mycpu());
  arg_buf arg_bug_msg;
  // retrieve command line arguements
  size_t argc = parse_args(&arg_bug_msg);
  // load_bincode_from_host_elf() is defined in kernel/elf.c
  load_bincode_from_host_elf(proc,arg_bug_msg.argv[mycpu()]);
  return proc;
}

//
// s_start: S-mode entry point of riscv-pke OS kernel.
//
int s_start(void) {
  // in the beginning, we use Bare mode (direct) memory mapping as in lab1.
  // but now, we are going to switch to the paging mode @lab2_1.
  // note, the code still works in Bare mode when calling pmm_init() and kern_vm_init().
  write_csr(satp, 0);
  if(mycpu()==0)
  {
    // init phisical memory manager
    pmm_init();

    // build the kernel page table
    kern_vm_init();
  
    enable_paging();

    // added @lab3_1
    init_proc_pool();

    // init file system, added @lab4_1
    fs_init();
  }
  sync_barrier(&barrier,NCPU);
  // now, switch to paging mode by turning on paging (SV39)

  insert_to_ready_queue(load_user_program());

  sprint("hartid = %d: Switch to user mode...\n",mycpu());
  schedule();
  // we should never reach here.
  return 0;
}
