/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"
#include "elf.h"
#include "spike_interface/spike_utils.h"

extern Symbols symbols[64];
extern int count;

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  sprint(buf);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
}

ssize_t sys_user_backtrace(uint64 n){
  uint64 current_fp =current->trapframe->regs.s0+16;
  int i = 0;
  for (i;i<n;i++)
  {
    // sprint("fp%d:0x%x\n",i,*(uint64*)(current_fp-8)); 
    uint64 code = *(uint64*)(current_fp-8);
    int j;
    for(j=0;j<count-1;j++){
      if (symbols[j+1].off<code && code<symbols[j].off)
        sprint("%s\n",symbols[j+1].name);
    }
    current_fp = *(uint64*)(current_fp-16);  
  }

  // int i = 0;
  // for(i=0;i<count;i++)
  //   sprint("0x%lx    %s\n",symbols[i].off,symbols[i].name);

  return 0;
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    case SYS_user_backtrace:
      return sys_user_backtrace(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
