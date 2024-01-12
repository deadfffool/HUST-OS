#include "user_lib.h"
#include "util/types.h"

int main(int argc, char *argv[]) {
  printu("\n======== exec /bin/app_ls in app_exec ========\n");
  int ret = exec("/bin/app_ls");
  printu("exec failed!\n");
  exit(-1);
}
