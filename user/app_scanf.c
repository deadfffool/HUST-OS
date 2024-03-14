#include "user/user_lib.h"
#include "util/types.h"

int main(void)
{
  char *string[128];
  printu("input: ");
  scanfu("%s", string);
  printu("output: %s\n", string);
  exit(0);
  return 0;
}