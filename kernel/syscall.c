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
#include "pmm.h"
#include "vmm.h"
#include "sched.h"
#include "proc_file.h"
#include "elf.h"

#include "spike_interface/spike_utils.h"

extern Symbols symbols[64];
extern int count;

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  // buf is now an address in user space of the given app's user stack,
  // so we have to transfer it into phisical address (kernel is running in direct mapping).
  assert( current );
  char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
  sprint(pa);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // reclaim the current process, and reschedule. added @lab3_1
  free_process( current );
  schedule();
  return 0;
}

//
// maybe, the simplest implementation of malloc in the world ... added @lab2_2
//
uint64 sys_user_allocate_page() {
  void* pa = alloc_page();
  uint64 va;
  // if there are previously reclaimed pages, use them first (this does not change the
  // size of the heap)
  if (current->user_heap.free_pages_count > 0) {
    va =  current->user_heap.free_pages_address[--current->user_heap.free_pages_count];
    assert(va < current->user_heap.heap_top);
  } else {
    // otherwise, allocate a new page (this increases the size of the heap by one page)
    va = current->user_heap.heap_top;
    current->user_heap.heap_top += PGSIZE;

    current->mapped_info[HEAP_SEGMENT].npages++;
  }
  user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ, 1));

  return va;
}

//
// reclaim a page, indicated by "va". added @lab2_2
//
uint64 sys_user_free_page(uint64 va) {
  user_vm_unmap((pagetable_t)current->pagetable, va, PGSIZE, 1);
  // add the reclaimed page to the free page list
  current->user_heap.free_pages_address[current->user_heap.free_pages_count++] = va;
  return 0;
}

//
// kerenl entry point of naive_fork
//
ssize_t sys_user_fork() {
  sprint("User call fork.\n");
  return do_fork( current );
}

// added lab3_c3 cow fork
ssize_t sys_user_cow_fork() {
  sprint("User call fork.\n");
  return do_cow_fork( current );
}

//
// kerenl entry point of yield. added @lab3_2
//
ssize_t sys_user_yield() {
  // TODO (lab3_2): implment the syscall of yield.
  // hint: the functionality of yield is to give up the processor. therefore,
  // we should set the status of currently running process to READY, insert it in
  // the rear of ready queue, and finally, schedule a READY process to run.
  current->status = READY;
  insert_to_ready_queue(current);
  schedule();
  return 0;
}

//
// wait
//
ssize_t sys_user_wait(long pid) {
  return do_wait(pid);
}

//
// open file
//
ssize_t sys_user_open(char *pathva, int flags) {
  char* pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathva);
  char resultpath[256];
  if(*pathpa == '.' && *(pathpa+1) != '.')
  {
    strcpy(resultpath,current->pfiles->cwd->name);
    if(!strcmp(resultpath,"/")) *resultpath=0;
    strcat(resultpath,(pathpa+1));
  }
  else if( *pathpa == '.' && *(pathpa+1) == '.')
  {
    if(current->pfiles->cwd->parent){
      strcpy(resultpath,current->pfiles->cwd->parent->name);
      if(!strcmp(resultpath,"/")) *resultpath=0;
      strcat(resultpath,(pathpa+2));
    }else
      strcpy(resultpath,"/");
  }
  else
    strcpy(resultpath,pathpa);

  return do_open(resultpath, flags);
}

//
// read file
//
ssize_t sys_user_read(int fd, char *bufva, uint64 count) {
  int i = 0;
  while (i < count) { // count can be greater than page size
    uint64 addr = (uint64)bufva + i;
    uint64 pa = lookup_pa((pagetable_t)current->pagetable, addr);
    uint64 off = addr - ROUNDDOWN(addr, PGSIZE);
    uint64 len = count - i < PGSIZE - off ? count - i : PGSIZE - off;
    uint64 r = do_read(fd, (char *)pa + off, len);
    i += r; if (r < len) return i;
  }
  return count;
}

//
// write file
//
ssize_t sys_user_write(int fd, char *bufva, uint64 count) {
  int i = 0;
  while (i < count) { // count can be greater than page size
    uint64 addr = (uint64)bufva + i;
    uint64 pa = lookup_pa((pagetable_t)current->pagetable, addr);
    uint64 off = addr - ROUNDDOWN(addr, PGSIZE);
    uint64 len = count - i < PGSIZE - off ? count - i : PGSIZE - off;
    uint64 r = do_write(fd, (char *)pa + off, len);
    i += r; if (r < len) return i;
  }
  return count;
}

//
// lseek file
//
ssize_t sys_user_lseek(int fd, int offset, int whence) {
  return do_lseek(fd, offset, whence);
}

//
// read vinode
//
ssize_t sys_user_stat(int fd, struct istat *istat) {
  struct istat * pistat = (struct istat *)user_va_to_pa((pagetable_t)(current->pagetable), istat);
  return do_stat(fd, pistat);
}

//
// read disk inode
//
ssize_t sys_user_disk_stat(int fd, struct istat *istat) {
  struct istat * pistat = (struct istat *)user_va_to_pa((pagetable_t)(current->pagetable), istat);
  return do_disk_stat(fd, pistat);
}

//
// close file
//
ssize_t sys_user_close(int fd) {
  return do_close(fd);
}

//
// lib call to opendir
//
ssize_t sys_user_opendir(char * pathva){
  char * pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathva);
  return do_opendir(pathpa);
}

//
// lib call to readdir
//
ssize_t sys_user_readdir(int fd, struct dir *vdir){
  struct dir * pdir = (struct dir *)user_va_to_pa((pagetable_t)(current->pagetable), vdir);
  return do_readdir(fd, pdir);
}

//
// lib call to mkdir
//
ssize_t sys_user_mkdir(char * pathva){
  char * pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), pathva);
  return do_mkdir(pathpa);
}

//
// lib call to closedir
//
ssize_t sys_user_closedir(int fd){
  return do_closedir(fd);
}

//
// lib call to link
//
ssize_t sys_user_link(char * vfn1, char * vfn2){
  char * pfn1 = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)vfn1);
  char * pfn2 = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)vfn2);
  return do_link(pfn1, pfn2);
}

//
// lib call to unlink
//
ssize_t sys_user_unlink(char * vfn){
  char * pfn = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)vfn);
  return do_unlink(pfn);
}


//
// exec
//
uint64 sys_user_exec(char * filename, char * para)
{
  char * file_name;
  char * para_;
  file_name = (char*)user_va_to_pa((pagetable_t)(current->pagetable), filename);
  para_ = (char*)user_va_to_pa((pagetable_t)(current->pagetable), para);
  do_exec(file_name,para_);
  return -1;
}

// added @lab1c1
ssize_t sys_user_backtrace(uint64 n){
  // uint64 current_fp =(current->trapframe->regs.s0+16);
  // int i = 0;
  // for (i;i<n;i++)
  // {
    // sprint("fp%d:0x%x\n",i,*(uint64*)(current_fp-8)); 
    // uint64 code = *(uint64*)(current_fp-8);
    int j;
    for(j=count-1;j>count-2-n;j--){
      // if (symbols[j+1].off<code && code<symbols[j].off)
        sprint("%s\n",symbols[j+1].name);
    }
    // current_fp = *(uint64*)(current_fp-16);  
  // }

  // int i = 0;
  // for(i=0;i<count;i++)
  //   sprint("0x%lx    %s\n",symbols[i].off,symbols[i].name);

  return 0;
}

ssize_t sys_user_rcwd(char *path){
  strcpy((char*)user_va_to_pa((pagetable_t)current->pagetable,path),current->pfiles->cwd->name);
  return 0;
}

ssize_t sys_user_ccwd(char *path){
  char resultpath[256];
  char* pathpa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), path);
  if(*pathpa == '.' && *(pathpa+1) != '.')
  {
    strcpy(resultpath,current->pfiles->cwd->name);
    if(!strcmp(resultpath,"/")) *resultpath=0; 
    strcat(resultpath,(pathpa+1));
  }
  else if( *pathpa == '.' && *(pathpa+1) == '.')
  {
    if(current->pfiles->cwd->parent){
      strcpy(resultpath,current->pfiles->cwd->parent->name);
      if(!strcmp(resultpath,"/")) *resultpath=0; //ignore the "/"
      strcat(resultpath,pathpa);
    }else
      strcpy(resultpath,"/");
  }
  else
    strcpy(resultpath,pathpa);
  if((ssize_t)strcpy(current->pfiles->cwd->name,resultpath)==0) 
    return -1;
  return 0;
}


// added lab2_c2
uint64 sys_user_malloc(uint64 size) {
  uint64 va = better_alloc(size);
  return va;
}

uint64 sys_user_free(uint64 va) {
  better_free(va);
  return 0;
}

//added @lab3_challenge2  sem_new
long sys_user_sem_new(int resource)
{
  //allocate a signal to current process's semaphore
  //and assign an initial value
  return do_sem_new(resource);
  
}

//added @lab3_challenge2 sem_P
long sys_user_sem_P(int mutex)
{
  //P operation
  do_sem_P(mutex);
  return 0;
}


//added @lab3_challenge2 sem_V
long sys_user_sem_V(int mutex)
{
  //V operation
  do_sem_V(mutex);
  return 0;
}

// added lab3c3
ssize_t sys_user_printpa(uint64 va)
{
  uint64 pa = (uint64)user_va_to_pa((pagetable_t)(current->pagetable), (void*)va);
  sprint("%lx\n", pa);
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
    case SYS_user_allocate_page:
      return sys_user_allocate_page();
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    case SYS_user_fork:
      return sys_user_fork();
    case SYS_user_yield:
      return sys_user_yield();
    case SYS_user_wait:
      return sys_user_wait(a1);
    case SYS_user_open:
      return sys_user_open((char *)a1, a2);
    case SYS_user_read:
      return sys_user_read(a1, (char *)a2, a3);
    case SYS_user_write:
      return sys_user_write(a1, (char *)a2, a3);
    case SYS_user_lseek:
      return sys_user_lseek(a1, a2, a3);
    case SYS_user_stat:
      return sys_user_stat(a1, (struct istat *)a2);
    case SYS_user_disk_stat:
      return sys_user_disk_stat(a1, (struct istat *)a2);
    case SYS_user_close:
      return sys_user_close(a1);
    case SYS_user_opendir:
      return sys_user_opendir((char *)a1);
    case SYS_user_readdir:
      return sys_user_readdir(a1, (struct dir *)a2);
    case SYS_user_mkdir:
      return sys_user_mkdir((char *)a1);
    case SYS_user_closedir:
      return sys_user_closedir(a1);
    case SYS_user_link:
      return sys_user_link((char *)a1, (char *)a2);
    case SYS_user_unlink:
      return sys_user_unlink((char *)a1);
    case SYS_user_exec:
      return sys_user_exec((char *)a1, (char *)a2);
    case SYS_user_backtrace:
      return sys_user_backtrace(a1);
    case SYS_user_rcwd:
      return sys_user_rcwd((char *)a1);
    case SYS_user_ccwd:
      return sys_user_ccwd((char *)a1);
    case SYS_user_malloc:
      return sys_user_malloc(a1);
    case SYS_user_free:
      return sys_user_free(a1);
    case SYS_user_sem_new:
      return sys_user_sem_new(a1);
    case SYS_user_sem_P:
      return sys_user_sem_P(a1);
    case SYS_user_sem_V:
      return sys_user_sem_V(a1);  
    case SYS_user_printpa:
      return sys_user_printpa(a1);
    case SYS_user_cow_fork:
      return sys_user_cow_fork(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
