/*
 * Below is the given application for lab1_challenge1_backtrace.
 * This app prints all functions before calling print_backtrace().
 */

#include "user_lib.h"
#include "util/types.h"

void f8()
{
  printu("back trace the user app in the following:\n");
  print_backtrace(8);
}
void f7() { f8(); }
void f6() { f7(); }
void f5() { f6(); }
void f4() { f5(); }
void f3() { f4(); }
void f2() { f3(); }
void f1() { f2(); }

int main(int argc, char *argv[])
{
  f1();
  exit(0);
  return 0;
}