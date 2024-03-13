#include "user/user_lib.h"
#include "util/types.h"

int main(void)
{
  char *string[128];
  scanfu("%s", string);
  printu("%s\n", string);
  exit(0);
  return 0;
}