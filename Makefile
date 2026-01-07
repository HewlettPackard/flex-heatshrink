##
#  SPDX-FileCopyrightText:  Copyright Hewlett Packard Enterprise Development LP
#  SPDX-License-Identifier:  MIT
##

all : FhdTest Example

CFLAGS:=-O2 -g -I.

FhdTest : Examples/FhdTest.c
	gcc -o $@ $< $(CFLAGS)

FhdTestSize : Examples/FhdTestSize.c
	gcc -o $@ $< -I. -Os -nostdlib -Wl,-e"start" -ffunction-sections -fdata-sections -flto -Wl,--gc-sections

# Run tests:  use heatshrink to compress a convenient binary, then
# decompress it using Flexible Heatshrink Decoder.  Run other tests
# using pre-computed data as well.  Also check the size of a minimal
# decompressor.
# Heatshrink must be available in $PATH.  Install it from your distro,
# or build it from source (https://github.com/atomicobject/heatshrink/).
test : FhdTest FhdTestSize
	heatshrink -e -w 9 -l 5 /usr/bin/gcc gcc.hs
	./FhdTest
	diff gcc.check /usr/bin/gcc
	./FhdTestSize
	size -A FhdTestSize | grep "bss\|text\|data"

Example : Example.c
	gcc -o $@ $< $(CFLAGS)

clean :
	rm -rf FhdTest gcc.hs gcc.check FhdTestSize Example

FhdTest FhdTestSize Example: FlexHeatshrink.h
