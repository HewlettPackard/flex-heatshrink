/**
*  SPDX-FileCopyrightText:  Copyright Hewlett Packard Enterprise Development LP
*  SPDX-License-Identifier:  MIT
**/

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#include <asm/unistd.h>      // compile without -m32 for 64 bit call numbers

typedef struct fhd_context_s_memory_func fhd_context_memory_func;

static
inline
int
ProduceStdout (
  fhd_context_memory_func *Context,
  uint8_t                 Byte
  );

// Memory to memory, with custom window and backref size
#define FHD_INPUT_MEMORY
#define FHD_OUTPUT_FUNC(C, Byte) ProduceStdout((C), (Byte))
#define FHD_FIXED_WINDOW 5
#define FHD_FIXED_BACKREF 4

#define FHD_IMPLEMENTATION
#include "FlexHeatshrink.h"

/**
  Output function that writes a byte to stdout using syscall

  This function receives decompressed bytes from the decoder and writes them
  directly to stdout (file descriptor 0) using the write syscall via inline
  assembly. This avoids the overhead of the C library write() wrapper,
  minimizing code size for the test program.

  @param   Context  Pointer to the memory-to-function decompression context (unused)
  @param   Byte     The decompressed byte to write to stdout

  @return  FHD_RET_SUCCESS  Byte successfully written to stdout
  @return  -10              Write syscall failed

**/
static
inline
int
ProduceStdout (
  fhd_context_memory_func *Context,
  uint8_t                 Byte
  )
{
  //return write( 0, &Byte, 1 ) == 1;
  int Ret;
  asm volatile
    (
     "syscall"
     : "=a" (Ret)
       //                 EDI      RSI       RDX
     : "0"(__NR_write), "D"(/*fd*/0), "S"(&Byte), "d"(1)
     : "rcx", "r11", "memory"
     );
  return (Ret == 1) ? FHD_RET_SUCCESS : -10;
}

// echo "hello world, how are hello world, you doing" | heatshrink -e -w 5 -l 4 | xxd -i
const
uint8_t
TestData[] = {
	0xb4, 0x59, 0x6d, 0x96, 0xcb, 0x7c, 0x82, 0xef, 0x6f, 0xb9, 0x5b, 0x2c,
	0x92, 0xc9, 0x05, 0xa2, 0xdf, 0x77, 0x90, 0x58, 0x6e, 0x56, 0x51, 0xc5,
	0x4b, 0xbc, 0xdb, 0xee, 0xb2, 0x0b, 0x25, 0xbe, 0xd3, 0x6e, 0xb3, 0xc2,
	0x80
};

fhd_context_memory_func Context;

/**
  Tiny sample program for memory-to-function decompression

  This program decompresses the TestData[] array with as little code
  as possible.  It maximizes inlining and compile-time code
  elminination by using a hardcoded ProduceStdout() routine and using
  fixed numbers of window and backref bits.

  This program is designed to be compiled without libc, so the exit()
  syscall is implemented using inline assembly.

  @return  0   Success

**/
int
start (
  void
  )
{
  int Ret;

  // Context is automatically zeroed with bss.
  Context.InputBuffer = TestData;
  Context.InputLen = sizeof (TestData);

  fhd_memory_func (&Context);

  // exit(0)
  asm volatile
    (
     "syscall"
     : "=a" (Ret)
       //                 EDI
     : "0"(__NR_exit), "D"(/*fd*/0), "S"(0), "d"(0)
     : "memory"
     );
}
