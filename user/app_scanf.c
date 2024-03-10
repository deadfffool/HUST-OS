#include "user/user_lib.h"
#include "util/types.h"

int main(void) {
  char * string = naive_malloc();
  scanfu("%s",string);
  printu("%s",string);
  return 0;
}