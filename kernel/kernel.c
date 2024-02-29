/*
 * Supervisor-mode startup codes
 */

#include "riscv.h"
#include "string.h"
#include "elf.h"
#include "process.h"
#include "config.h"
#include "spike_interface/spike_utils.h"

// process is a structure defined in kernel/process.h
process user_app[NCPU];

void load_user_program(process *proc) {
  // set hart id for this proc
  uint64 hart_id = mycpu();
  // USER_TRAP_FRAME is a physical address defined in kernel/config.h
  // USER_KSTACK is also a physical address defined in kernel/config.h
  // USER_TRAP_FRAME is a physical address defined in kernel/config.h
  proc->trapframe = (trapframe *)USER_TRAP_FRAME + HART_STACK_OFFSET * hart_id;
  memset(proc->trapframe, 0, sizeof(trapframe));
  proc->kstack = USER_KSTACK + HART_STACK_OFFSET * hart_id;
  proc->trapframe->regs.sp = USER_STACK + HART_STACK_OFFSET * hart_id;
}

//
// s_start: S-mode entry point of riscv-pke OS kernel.
//
int s_start(void) {
  uint64 hartid = mycpu();
  sprint("hartid = %d: Enter supervisor mode...\n",hartid);
  // Note: we use direct (i.e., Bare mode) for memory mapping in lab1.
  // which means: Virtual Address = Physical Address
  // therefore, we need to set satp to be 0 for now. we will enable paging in lab2_x.
  // 
  // write_csr is a macro defined in kernel/riscv.h
  write_csr(satp, 0);
  // the application code (elf) is first loaded into memory, and then put into execution
  load_user_program(&user_app[hartid]);

  // load_bincode_from_host_elf() is defined in kernel/elf.c
  load_bincode_from_host_elf(&user_app[hartid]);

  sprint("hartid = %d: Switch to user mode...\n",hartid);
  // switch_to() is defined in kernel/process.c
  switch_to(&user_app[hartid]);

  // we should never reach here.
  return 0;
}
