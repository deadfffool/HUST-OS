#include "kernel/riscv.h"
#include "kernel/process.h"
#include "spike_interface/spike_utils.h"
#include "util/string.h"

static void handle_instruction_access_fault() { panic("Instruction access fault!"); }

static void handle_load_access_fault() { panic("Load access fault!"); }

static void handle_store_access_fault() { panic("Store/AMO access fault!"); }

static void handle_illegal_instruction() { panic("Illegal instruction!"); }

static void handle_misaligned_load() { panic("Misaligned Load!"); }

static void handle_misaligned_store() { panic("Misaligned AMO!"); }

char errorfile_path[8192], errorline_code[32768];

struct stat error_stat;
static void print_errorline()
{
  int i = 0;
  uint64 mepc = read_csr(mepc);
  for (i = 0; i < current->line_ind; i++)
  {
    // sprint("mepc: %lx, current->line[%d].addr: %lx\n", mepc, i, current->line[i].addr);
    if (mepc < current->line[i].addr) // find the error line
    {
      addr_line *error_line = current->line + i - 1;
      strcpy(errorfile_path, current->dir[current->file[error_line->file].dir]);
      strcat(errorfile_path, "/");
      strcat(errorfile_path, current->file[error_line->file].file);
      // read the code line and print
      spike_file_t *file = spike_file_open(errorfile_path, O_RDONLY, 0);
      spike_file_stat(file, &error_stat);
      spike_file_read(file, errorline_code, error_stat.st_size);
      int cnt = 0, j = 0;
      while (j < error_stat.st_size)
      {
        int code_ind;
        for (code_ind = j; code_ind < error_stat.st_size && errorline_code[code_ind] != '\n'; code_ind++)
        {
        }
        if (cnt == error_line->line - 1)
        {
          errorline_code[code_ind] = '\0';
          sprint("Runtime error at %s:%d\n", errorfile_path, error_line->line);
          sprint("%s\n", errorline_code + j);
          break;
        }
        else
        {
          j = 1 + code_ind;
          cnt++;
        }
      }
      spike_file_close(file);
      break;
    }
  }
}


// added @lab1_3
static void handle_timer() {
  int cpuid = 0;
  // setup the timer fired at next time (TIMER_INTERVAL from now)
  *(uint64*)CLINT_MTIMECMP(cpuid) = *(uint64*)CLINT_MTIMECMP(cpuid) + TIMER_INTERVAL;

  // setup a soft interrupt in sip (S-mode Interrupt Pending) to be handled in S-mode
  write_csr(sip, SIP_SSIP);
}

//
// handle_mtrap calls a handling function according to the type of a machine mode interrupt (trap).
//
void handle_mtrap() {
  uint64 mcause = read_csr(mcause);
  switch (mcause) {
    case CAUSE_MTIMER:
      handle_timer();
      break;
    case CAUSE_FETCH_ACCESS:
      print_errorline();
      handle_instruction_access_fault();
      break;
    case CAUSE_LOAD_ACCESS:
      print_errorline();
      handle_load_access_fault();
    case CAUSE_STORE_ACCESS:
      print_errorline();
      handle_store_access_fault();
      break;
    case CAUSE_ILLEGAL_INSTRUCTION:
      print_errorline();
      handle_illegal_instruction();
      break;
    case CAUSE_MISALIGNED_LOAD:
      print_errorline();
      handle_misaligned_load();
      break;
    case CAUSE_MISALIGNED_STORE:
      print_errorline();
      handle_misaligned_store();
      break;

    default:
      sprint("machine trap(): unexpected mscause %p\n", mcause);
      sprint("            mepc=%p mtval=%p\n", read_csr(mepc), read_csr(mtval));
      panic( "unexpected exception happened in M-mode.\n" );
      break;
  }
}
