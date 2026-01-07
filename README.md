<!-- SPDX-FileCopyrightText:  Copyright Hewlett Packard Enterprise Development LP -->
<!-- SPDX-License-Identifier:  MIT -->
# Flexible Heatshrink Decoder (FHD)

This is a single-file-header version of the [heatshrink](https://github.com/atomicobject/heatshrink/)
decompression algorithm, rewritten for embedded and application-specific environments, able
to be configured for in-place decompression, chunked operation, callbacks, byte-by-byte
streaming, or almost anything you desire.  Most API elements (buffers, callbacks, 
compresion parameters, etc.) can be hardcoded or set dynamically as the caller 
requires.  The caller has complete flexibility for memory management and execution 
flow:  you can make the decompressor fit your algorithm, not the other way around.

FHD has no dependencies other than `stdint.h`.  It can be #included multiple times with 
different configuration macros from a single 
source file, to define multiple decompression routines with differing APIs.

FHD is designed to be extremely small as well.  Typically it compiles to around about 500 bytes of code.
In fact a complete demo in Examples/FhdTestSize.c compiles to 615 bytes of code, has no
dependencies (`-nostdlib`) and runs in Linux on x86_64.  And it only requires 56 bytes
of RAM for its context structure, including a 32-byte history buffer.

This repository also contains example code and a test harness.  Use "make test" to build
and run the test code.  You will need the heatshrink encoder available in your $PATH.
Either install it from your distro (for example, `sudo apt install heatshrink` on Debian
or Ubuntu) or build it yourself from the [sources](https://github.com/atomicobject/heatshrink/).

## General notes about the heatshrink algorithm

1. Heatshrink uses a configurable window/max history count and backreference run size.
2. There is no header, so you MUST use the same window/max history and backreference size in decode as were used for encode.
3. Compression does not have an "end of stream" indicator, so you can just stop when you run out of data or otherwise know you are finished.
4. You MUST have a history buffer of at least `1<<window_bits` bytes, because the decompressor references previously-generated strings.  However, you can use the output itself as the history buffer when it is available.  This is the default when decompressing to memory.  Otherwise, by default the history buffer is stored in the context structure, increasing its size.

## Options

To use FHD, you must define one FHD_INPUT_* preprocessor symbol, and one FHD_OUTPUT_* preprocessor symbol.
- Input options:
  - FHD_INPUT_MEMORY - Pass the entire input in memory
  - FHD_INPUT_BUFFER - Pass a chunk of input with each call
  - FHD_INPUT_FUNC - Call a function for each input character
  - FHD_INPUT_PUTC - Pass one character of input with each call
  - FHD_INPUT_CUSTOM - Whatever you like
- Output options:
  - FHD_OUTPUT_MEMORY - Write entire output to memory
  - FHD_OUTPUT_BUFFER - Write a chunk of output with each call
  - FHD_OUTPUT_FUNC - Call a function for each output character
  - FHD_OUTPUT_GETC - Return output characters one at a time
  - FHD_OUTPUT_CUSTOM - Whatever you like

The input and output options are incorporated into the names of the decompression routine
and the context structure, so you can #include FlexHeatshrink.h multiple times from a 
single file without name collisions.  See [Examples/FhdTest.c](Examples/FhdTest.c) for many examples.

Numerous additional configuration options are available for history management, 
window/history and backreference size configuration, user extensions, etc.
See the full documentation and usage discussion in [FlexHeatshrink.h](FlexHeatshrink.h).

Note:  you must define FHD_IMPLEMENTATION to generate the actual decoder, rather than just
a declaration of it.
```c
    ...
<Define donfiguration macros>
    ...
#define FHD_IMPLEMENTATION
#include "FlexHeatshrink.h"
```

## Example
This example performs simple memory-to-memory decompression, and outputs the result.

```c
#include <stdio.h>
#include <string.h>

// Configure the input and output interfaces for the decompressor
#define FHD_INPUT_MEMORY
#define FHD_OUTPUT_MEMORY
// Define FHD_IMPLEMENTATION to generate the decompressor code, not just
// declare the interface.
#define FHD_IMPLEMENTATION
#include "FlexHeatshrink.h"

// echo "hello world, how are hello world, you doing" | heatshrink -e -w 8 -l 4 | xxd -i
uint8_t test_data[] = { 
  0xb4, 0x59, 0x6d, 0x96, 0xcb, 0x7c, 0x82, 0xef, 0x6f, 0xb9, 0x5b, 0x2c,
  0x92, 0xc9, 0x05, 0xa2, 0xdf, 0x77, 0x90, 0x58, 0x6e, 0x56, 0x50, 0x38,
  0x85, 0x2e, 0xf3, 0x6f, 0xba, 0xc8, 0x2c, 0x96, 0xfb, 0x4d, 0xba, 0xcf,
  0x0a
};

int
main(int argc, char **argv)
{
  int                       status;
  uint8_t                   output_buffer[256]; // Must be at least 1<<windowsize 
  fhd_context_memory_memory ctx;

  // Ensure output is NUL-terminated
  memset(output_buffer, 0, sizeof (output_buffer));

  // Initialize the context structure.  After zeroing it, set the fields
  // relevant to the selected FHD_INPUT_* and FHD_OUTPUT_* settings.
  memset(&ctx, 0, sizeof (ctx));
  ctx.input_buffer  = test_data;
  ctx.input_len     = sizeof(test_data);
  ctx.output_buffer = output_buffer;
  ctx.output_len    = sizeof(output_buffer);
  ctx.window_bits   = 8;
  ctx.backref_bits  = 4;

  // Decompress the input data to the output buffer
  status = fhd_memory_memory (&ctx);
  if (FHD_RET_SUCCESS != status) {
    fprintf(stderr, "Error! Status: %d\n", status);
    return status;
  }

  puts(output_buffer);
  return 0;
}
```

## Quick comparison

| File | Initial Size | gzip -9 | heatshrink -w 8 -l 4 |
| --- | --- | --- | --- |
| x86_64 gcc 11 | 928584 | 337702 (36%) | 478246 (51%) |
| BadApple-mod.mid | 13040 | 1009 (7.7%) | 2394 (18%) |
| spacestations.txt (tle) | 5378 | 1797 (33%) | 2800 (52%) |

As you can see, heatshrink is not as good as gzip, but in some cases it still can compress rather normal things by around 50%!

## Background

Heatshrink, itself, is like gzip but without all the fancy tables.  It uses 
[LZSS](https://en.wikipedia.org/wiki/Lempel%E2%80%93Ziv%E2%80%93Storer%E2%80%93Szymanski)
compression, but does not leverage huffman tables which are common in more sophisticated
compression algorithms.  Compression is not as good as deflate, but it's still pretty
impressive what a simple algorithm can accomplish.
