#include "user/user_lib.h"
#include "util/types.h"

int main(void)
{
  char *string[128];
  printu("please enter what you want to print: ");
  scanfu("%s", string);
  printu("user enter: %s\n", string);
  exit(0);
  return 0;
}