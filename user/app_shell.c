/*
 * This app starts a very simple shell and executes some simple commands.
 * The commands are stored in the hostfs_root/shellrc
 * The shell loads the file and executes the command line by line.
 */
#include "user_lib.h"
#include "string.h"
#include "util/types.h"

int main(int argc, char *argv[])
{
  printu("\nWelcome from hart 0! \n\n");
  char command[128];
  char para[128];
  char cwd[128];
  while (1)
  {
    read_cwd(cwd);
    printu("miles@Chenxuan-MacBook %s $ ", cwd);
    scanfu("%s %s", command, para);
    if (strcmp(command, "q") == 0)
      break;
    else if (strcmp("cd", command) == 0)
    {
      change_cwd(para);
      continue;
    }
    else
    {
      printu("\n==========Command Start============\n");
      int pid = fork();
      if (pid == 0)
      {
        int ret = exec(command, para);
        if (ret == -1)
          printu("exec failed!\n");
      }
      else
      {
        wait(pid);
        printu("==========Command End============\n\n");
      }
    }
  }
  exit(0);
  return 0;
}