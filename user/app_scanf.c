#include "user/user_lib.h"
#include "util/types.h"

int main(void) {
  char * string = naive_malloc();
  scanfu("%s",string);
  printu("%s\n",string);
  exit(0);
  return 0;
}