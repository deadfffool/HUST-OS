#ifndef _ELF_H_
#define _ELF_H_

#include "util/types.h"
#include "process.h"

#define MAX_CMDLINE_ARGS 64
#define STT_FUNC  2    //st_info
#define STB_GLOBAL (1<<4)   //st_info
#define MAX_DEBUGLINE 10000

// elf header structure
typedef struct elf_header_t {
  uint32 magic;
  uint8 elf[12];
  uint16 type;      /* Object file type */
  uint16 machine;   /* Architecture */
  uint32 version;   /* Object file version */
  uint64 entry;     /* Entry point virtual address */
  uint64 phoff;     /* Program header table file offset */
  uint64 shoff;     /* Section header table file offset */
  uint32 flags;     /* Processor-specific flags */
  uint16 ehsize;    /* ELF header size in bytes */
  uint16 phentsize; /* Program header table entry size */
  uint16 phnum;     /* Program header table entry count */
  uint16 shentsize; /* Section header table entry size */
  uint16 shnum;     /* Section header table entry count */
  uint16 shstrndx;  /* Section header string table index */
} elf_header;

typedef struct elf_sect_header_t {
  uint32 name;		  /* Section name, index in string tbl */
  uint32 type;		  /* Type of section */
  uint64 flags;		/* Miscellaneous section attributes */
  uint64 addr;		  /* Section virtual addr at execution */
  uint64 offset;		/* Section file offset */
  uint64 size;		  /* Size of section in bytes */
  uint32 link;		  /* Index of another section */
  uint32 info;		  /* Additional section information */
  uint64 addralign;/* Section alignment */
  uint64 entsize;	/* Entry size if section holds table */
} elf_sect_header;

// elf symbol table structure
typedef struct elf_sym_t {
  uint32   name;
  uint8    info;      /* the type of symbol */
  uint8    other;
  uint16   shndx;
  uint64   value;     // address maybe
  uint64   size;
} elf_sym;

// segment types, attributes of elf_prog_header_t.flags
#define SEGMENT_READABLE   0x4
#define SEGMENT_EXECUTABLE 0x1
#define SEGMENT_WRITABLE   0x2

// Program segment header.
typedef struct elf_prog_header_t {
  uint32 type;   /* Segment type */
  uint32 flags;  /* Segment flags */
  uint64 off;    /* Segment file offset */
  uint64 vaddr;  /* Segment virtual address */
  uint64 paddr;  /* Segment physical address */
  uint64 filesz; /* Segment size in file */
  uint64 memsz;  /* Segment size in memory */
  uint64 align;  /* Segment alignment */
} elf_prog_header;

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian
#define ELF_PROG_LOAD 1

typedef enum elf_status_t {
  EL_OK = 0,

  EL_EIO,
  EL_ENOMEM,
  EL_NOTELF,
  EL_ERR,

} elf_status;

typedef struct elf_ctx_t {
  void *info;
  elf_header ehdr;
} elf_ctx;

typedef struct elf_info_t {
  struct file *f;
  process *p;
} elf_info;

// added lab1c1
typedef struct symbol_tab{
	char name[16];
	uint64 off;
} Symbols;

// added lab1c2
// compilation units header (in debug line section)
typedef struct __attribute__((packed)) {
    uint32 length;
    uint16 version;
    uint32 header_length;
    uint8 min_instruction_length;
    uint8 default_is_stmt;
    int8 line_base;
    uint8 line_range;
    uint8 opcode_base;
    uint8 std_opcode_lengths[12];
} debug_header;

elf_status elf_init(elf_ctx *ctx, void *info);
elf_status elf_load(elf_ctx *ctx);

void load_bincode_from_host_elf(process *p, char *filename);
void load_bincode_from_host_elf_name(process *p,char *filename);
void bubble_sort(Symbols arr[], int n);
void load_func_name(elf_ctx *ctx);
#endif
