/**
*  SPDX-FileCopyrightText:  Copyright Hewlett Packard Enterprise Development LP
*  SPDX-License-Identifier:  MIT
*
*   Copyright 2024 <>< cnlohr
**/
/** @file
  Flexible Heatshrink Decoder
  by Brian J. Johnson

  Heatshrink is an implementation of the LZSS compression algorithm, targeted
  for low-memory embedded environments.  See the Acknowledgements below.
  Flexible Heatshrink Decoder (FHD) is an implementation of the heatshrink
  decompression algorithm, packaged for maximum flexibility.  Memory management
  and I/O are up to the caller, and can be handled pretty much any way
  imaginable (provided by callbacks, placed in memory, split into single-packet
  buffers, provided a character at a time, etc.)  See the section on
  Configuration Macros below.

  In some configurations, the decompression routine can return before all data
  is decompressed (for example, if an output buffer is full.)  If this is
  expected for that configuration, it saves state data in a contex structure so
  it can resume decompression on the next call.  To do this without coding a
  complicated state machine, FHD relies on coroutine macros to pause and resume
  execution.  See the references in the Acknowledgements section for details.

  FHD is provided as a single-file-header, for ease of integration.  It has no
  dependencies other than stdint.h.  (This can be avoided by #defining
  FHD_NO_STDINT; see below.)  FHD can be #included multiple times with different
  configuration macros from a single source file, to define multiple
  decompression routines with differing APIs.


  Basic usage
  ===========

  #define one of the macros from the "Input" section below, and one from the
  "Output" section.  If you want the decompression routine to be compiled in
  this source file, instead of just declared, then #define FHD_IMPLEMENTATION as
  well.  After this header file is #included, the context structure and
  decompression routine will be declared like this:

    typedef struct fhd_context_s_<input>_<output> {
       ...
    } fhd_context_<input>_<output>;

    INTN fhd_<input>_<output>(fhd_context_<input>_<output> *C);

  where "<input>" and "<output>" are tags which vary depending on the input and
  output macros you defined.  This allows the header file to be #included
  multiple times in order to define multiple decompression interfaces.

  The "window bits" and "lookahead bits" parameters used during heatshrink
  compression also need to be specified.  Note that these are not encoded in the
  compressed data:  they need to be communicated through some other means.  By
  default they are specified in the "WindowBits" and "BackrefBits" context
  fields.  Alternatively, they can be provided by the FHD_USER_WINDOW() and
  FHD_USER_BACKREF() macros.  Or, if they are fixed values, they can be
  hardcoded via the FHD_FIXED_WINDOW and FHD_FIXED_BACKREF macros.  This will
  let the compiler better optimize the code.

  To decompress data, first allocate and zero the context structure, then
  initialize the required context fields.  These will vary depending on the
  configuration macros you chose.  Then pass a pointer to the context to the
  decompression routine, and check the return value.

  Some input/output methods can return to the caller after partially
  decompressing the input, for example when an input buffer has run out of data.
  In these cases, update the context structure as necessary (for example,
  provide the next chunk of input data) and call the decompression routine
  again.

  Example of memory-to-memory decompression:

  #define FHD_INPUT_MEMORY
  #define FHD_OUTPUT_MEMORY
  #define FHD_IMPLEMENTATION
  #include "FlexHeatshrink.h"

  INTN status;
  fhd_context_memory_memory Context;

  memset (&Context, 0, sizeof (Context));
  Context.InputBuffer  = my_input_data;
  Context.InputLen     = my_input_data_len;
  Context.OutputBuffer = my_output_buffer;
  Context.OutputLen    = my_output_buffer_len;
  Context.WindowBits   = my_window_size_bits;
  Context.BackrefBits  = my_backref_length_bits;

  status = fhd_memory_memory (&Context);
  if (FHD_RET_SUCCESS != status) {
    printf ("Status: %d\n", status);
    HandleError ();
  }


  Configuration Macros
  ====================

  One macro in the input category and one in the output category must be
  #defined when this header is #included, to specify its behavior and API.  They
  set various internal macros (FHD_INT_*) which are used in the actual code.

  The macros in the history category are optional.  FHD requires access to
  recently decompressed data in order to handle backreferences in the input
  stream.  If the output is not available directly in memory (i.e. not the
  FHD_OUTPUT_MEMORY case), it defaults to using a fixed-size buffer in the
  context structure to store this history.  That can be changed using these
  macros.  The macro FHD_HISTORY_BUFFER_SIZE(WindowBits) can be used to
  calculate the minimum history buffer size needed for a given number of history
  window bits.

  The macros in the window bits and backref bits categories are optional.  If
  they are not defined, they are taken from the WindowBits and BackrefBits
  fields of the context structure.  These fields must be set before calling the
  decompression routine.

  The FHD_INT* macros should not be defined by the caller, unless the
  FHD_INPUT_CUSTOM and/or FHD_OUTPUT_CUSTOM configurations are being used.  All
  FHD_INT* macros are undefined at the end of this header file, so it can be
  #included again with a different configuration.

  Input
  -----
  FHD_INPUT_MEMORY -- Tag: "memory"
    The entire input is in a single buffer in memory.  The caller must set these
    context fields:
      CONST UINT8 *InputBuffer  Pointer to the input buffer
      INTN        InputLen      Size of the input buffer
    A return value of FHD_RET_SUCCESS indicates successful decoding.

  FHD_INPUT_BUFFER -- Tag: "buffer"
    Part of the input is in memory.  A return value of FHD_RET_END_OF_INPUT
    indicates the buffer has been consumed.  A subsequent call to the decoding
    routine with a new input buffer will pick up decoding where it left off.
    The caller must set these context fields:
       CONST UINT8 *InputBuffer  Pointer to the input buffer
       INTN        InputLen      Length of the input buffer
       INTN        InputPlace    Location of the next character in the buffer
    During decompression, InputPlace is incremented until it reaches
    InputLen.  The other fields are not modified.

  FHD_INPUT_FUNC(Context) -- Tag: "func"
    Calls a function for each input character.  FHD_INPUT_FUNC() is #defined to
    the function to call, which should have this prototype:
        INTN InputFunction(fhd_context_<in>_<out> *C);
    It is passed a pointer to the context structure, and is expected to return a
    character (0-255) or a negative error code.  The decoder returns the error
    code.  FHD_INPUT_FUNC() can be a compile-time function, or a function
    pointer in the FHD_OPAQUE portion of the context.  To allow restarting after
    an error (eg. after refilling an input buffer), define FHD_USE_COROUTINE.

  FHD_INPUT_PUTC -- Tag: "putc"
    Input is passed to the decoding routine one character at a time, in this
    context field:
      UINT8 InputByte   Next byte to decode
    The byte passed with the initial call is ignored.  Each time the decode routine
    returns FHD_RET_END_OF_INPUT, the caller should place the next byte to
    decode into InputByte, and call the decoder again.

  FHD_INPUT_CUSTOM
    Whatever you want!  The caller must #define FHD_INT_GET_BYTE() and either
    FHD_INT_INPUT_TAG or FHD_INT_NAME(), along with any other required internal
    configuration macros.

  Output
  ------
  FHD_OUTPUT_MEMORY -- Tag: "memory"
    The entire output is a single buffer in memory.  This buffer is also used as
    the history buffer, so no separate history buffer is required.  The caller
    must set these context fields:
      UINT8 *OutputBuffer      Pointer to the output buffer
      INTN  OutputLen          Size of the output buffer
    A return value of FHD_RET_OVERFLOW indicates the buffer is too small.

  FHD_OUTPUT_BUFFER -- Tag: "buffer"
    Decode to a buffer in memory, which may be smaller than the entire output.
    A return value of FHD_RET_OVERFLOW indicates the buffer has been filled.  A
    subsequent call to the decoding routine with a new output buffer will pick
    up decoding where it left off.  The caller must set these context fields:
       CONST UINT8 *OutputBuffer  Pointer to the output buffer
       INTN        OutputLen      Length of the output buffer
       INTN        OutputPlace    Location of the next character in the buffer
    During decompression, OutputPlace is incremented until it reaches
    OutputLen.  The other fields are not modified.

  FHD_OUTPUT_FUNC(Context, Byte) -- Tag: "func"
    Calls a function for each output character.  FHD_OUTPUT_FUNC() is #defined
    to the function to call.  It is passed a pointer to the context structure and
    the character to output, with this prototype:
        INTN OutputFunction(fhd_context_<in>_<out> *C, UINT8 Byte);
    The return value should be FHD_RET_SUCCESS, or a negative error code.  The
    decoder returns the error code to its caller.  FHD_OUTPUT_FUNC() can be a
    compile-time function, or a function pointer in the FHD_OPAQUE portion of
    the context.  To allow restarting after an error (eg. after resizing or
    emptying an output buffer), define FHD_USE_COROUTINE.

  FHD_OUTPUT_GETC -- Tag: "getc"
    Returns output characters one at a time from the decoding routine.

  FHD_OUTPUT_CUSTOM
    Whatever you want!  The caller must #define FHD_INT_PUT_BYTE() and either
    FHD_INT_OUTPUT_TAG or FHD_INT_NAME(), along with any other required internal
    configuration macros.

  History
  -------
  Optional.  Defaults to a fixed history buffer in the context structure.

  FHD_HISTORY_USER_GET(Context, Index, Out)
    Set "Out" to the value "Index" bytes back in the history.

  FHD_HISTORY_USER_ADD(Context, Byte)
    Add "Byte" to the history.

  FHD_HISTORY_USER_OPAQUE
    History-related fields to add to the context structure.

  FHD_HISTORY_BUFFER_SIZE(WindowBits)
    This macro is defined by the header file, not the caller.  It can be used to
    calculate the minimum history buffer size needed for a given number of
    history window bits.

  Window Bits
  -----------
  Optional.  Defaults to Context->WindowBits.

  FHD_MAX_WINDOW
    The maximum number of history window bits to support.  Determines the
    default history buffer size.  If FHD_MAX_WINDOW <= 8, only one byte of index
    bits needs to be fetched, which slightly streamlines the decoder.  Defaults
    to FHD_FIXED_WINDOW if that is supplied, otherwise HEATSHRINK_MAX_WINDOW_BITS.

  FHD_FIXED_WINDOW
    Compile-time constant giving the number of history window bits, from
    HEATSHRINK_MIN_WINDOW_BITS to HEATSHRINK_MAX_WINDOW_BITS.  Also determines
    the default history buffer size.

  FHD_USER_WINDOW(Context)
    An arbitrary expression returning the number of history window bits.  May
    use the context fields defined in FHD_WINDOW_USER_OPAQUE.

  FHD_WINDOW_USER_OPAQUE
    Window-related fields to add to the context structure.

  Backreference Bits
  ------------------
  Optional.  Defaults to Context->BackrefBits.

  FHD_MAX_BACKREF
    The maximum number of backreference bits to support.  If FHD_MAX_BACKREF <=
    8, only one byte of backreference bits needs to be fetched, which slightly
    streamlines the decoder.  Defaults to FHD_FIXED_BACKREF if that is supplied,
    otherwise the default is the same as FHD_MAX_WINDOW.

  FHD_FIXED_BACKREF
    Compile-time constant giving the number of backreference length bits, from
    HEATSHRINK_MIN_LOOKAHEAD_BITS to the maximum number history window bits.
    (The number of backreference length bits can not exceed the number of
    history window bits.)

  FHD_USER_BACKREF(Context)
    An arbitrary expression returning the number of backreference length bits.
    May use the context fields defined in FHD_BACKREF_USER_OPAQUE.

  FHD_BACKREF_USER_OPAQUE
    Backreference-related fields to add to the context structure.

  Other
  -----
  Optional.

  FHD_OPAQUE
    Additional fields to be added to the context structure, for use by I/O
    callbacks or anything else the user desires.

  FHD_USE_COROUTINE
    Have the decompressor save state in the context structure so that
    decompression can resume after it returns.  It is enabled by default for
    input/output configurations which require it, but this macro will enable it
    unconditionally.  This can be useful for the FHD_{INPUT,OUTPUT}_FUNC
    configurations.

  FHD_DECORATOR
    Set this to an arbitrary decorator, such as "static" or "inline", which will
    be added to the "fhd_<input>_<output>()" declaration and definition.

  FHD_NO_STDINT
    FHD makes use of several standard data types:  unsigned 8- and 16-bit
    integers (UINT8, UINT16), and signed "naturally sized" integers (INT).
    Normally, it typedefs them itself using types from stdint.h.  If you define
    FHD_NO_STDINT, stdint.h will not be included, and you are responsible for
    providing you own definitions of UINT8, UINT16, and INT.

  FHD_NO_CONST
    FHD declares constant data types with the CONST keyword (UEFI convention.)
    By default, FHD defines CONST to "const" if it is not already defined.  If
    you define FHD_NO_CONST, you can supply your own definition of CONST.

  Internal configuration macros
  =============================
  These are set automatically as needed by the configuration macros above, but
  can be overridden for FHD_{INPUT,OUTPUT}_CUSTOM configuratinos.

  Input-related
  -------------
  FHD_INT_GET_BYTE(C, Byte) - Place the next input character in "Byte"
  FHD_INT_INPUT_TAG - String to add to context and decoder names
  FHD_INT_INPUT_OPAQUE - Context fields used by FHD_INT_GET_BYTE()

  Output-related
  --------------
  FHD_INT_PUT_BYTE(C, Byte) - Add "Byte" to the output
  FHD_INT_OUTPUT_TAG - String to add to context and decoder names
  FHD_INT_OUTPUT_OPAQUE - Context fields used by FHD_INT_PUT_BYTE()

  History-related
  ---------------
  FHD_INT_GET_HISTORY(C, Index, Out) - Set "Out" to the value "Index" bytes back in history
  FHD_INT_ADD_TO_HISTORY(C, Byte) - Add "Byte" to the history
  FHD_INT_HISTORY_OPAQUE - Context fields used by FHD_INT_{GET,ADD_TO}_HISTORY()

  Window-related
  --------------
  FHD_INT_WINDOW(C) - The number of window index bits
  FHD_INT_MAX_WINDOW - The maximum number of window index bits
  FHD_INT_WINDOW_OPAQUE - Context fields used by FHD_INT_WINDOW()

  Backref-related
  --------------
  FHD_INT_BACKREF(C) - The number of backreference length bits
  FHD_INT_MAX_BACKREF - The maximum number of backreference length bits
  FHD_INT_BACKREF_OPAQUE - Context fields used by FHD_INT_BACKREF()

  Other
  -----
  FHD_INT_OPAQUE - User-supplied context fields
  FHD_INT_USE_COROUTINE - Use Cor*() macros to save state and resume
  FHD_INT_NAME(Name)
    Generate an identifier reflecting the current I/O configuration, based on
    the input Name.  This macro is used to generate names for the context data
    type and the decoder routine.  By default, the name is based on
    FHD_INT_{INPUT,OUTPUT}_TAG.
  FHD_INT_DECORATOR - Decorator placed before the decompressor declaration/definition


  Acknowledgements
  ================
  FHD was derived (loosely) from cnlohr's heatshrink single-file-header decoder:
    https://github.com/cnlohr/heatshrink-sfh
  His code says, "This file may be licensed under the ISC, MIT new BSD or in
  public domain as the user desires."

  The coroutine macros are based on Simon Tatham's ideas and implementation:
    https://www.chiark.greenend.org.uk/~sgtatham/coroutines.html
    https://www.chiark.greenend.org.uk/~sgtatham/coroutine.h
**/

// Note: no multiple-#include guards.  This file is meant to be #included multiple times.


// Constants for the heatshrink file format, from the heatshrink encoder:
//   https://github.com/atomicobject/heatshrink/blob/develop/include/heatshrink_common.h

// Minima and maxima, needed by the configuration macros below
#define HEATSHRINK_MIN_WINDOW_BITS 4
#define HEATSHRINK_MAX_WINDOW_BITS 15
#define HEATSHRINK_MIN_LOOKAHEAD_BITS 3

// Other constants
#define HEATSHRINK_LITERAL_MARKER 0x01
#define HEATSHRINK_BACKREF_MARKER 0x00


//
// Parse the configuration macros
//

// Input

#if defined (FHD_INPUT_MEMORY)
  // Input is a single buffer in memory

  #if defined (FHD_INPUT_BUFFER) || defined (FHD_INPUT_FUNC) || defined (FHD_INPUT_PUTC) || defined (FHD_INPUT_CUSTOM)
  #error More than one FHD_INPUT_* macro is defined
  #endif

  #define FHD_INT_INPUT_TAG memory

  #define FHD_INT_INPUT_OPAQUE                  \
          CONST UINT8 *InputBuffer;             \
          INTN        InputLen;

  #define FHD_INT_GET_BYTE(C, Byte)             \
    do {                                        \
      if ((C)->InputLen-- > 0) {                \
        (Byte) = (C)->InputBuffer[0];           \
        (C)->InputBuffer++;                     \
      }                                         \
      else {                                    \
        return FHD_RET_SUCCESS;                 \
      }                                         \
    } while (0)


#elif defined(FHD_INPUT_BUFFER)
  // Input is a refillable buffer in memory

  #if defined (FHD_INPUT_MEMORY) || defined (FHD_INPUT_FUNC) || defined (FHD_INPUT_PUTC) || defined (FHD_INPUT_CUSTOM)
  #error More than one FHD_INPUT_* macro is defined
  #endif

  #define FHD_INT_INPUT_TAG buffer
  #define FHD_INT_USE_COROUTINE
  #define FHD_INT_INPUT_OPAQUE                \
          CONST UINT8 *InputBuffer;           \
          INTN        InputLen;               \
          INTN        InputPlace;

  #define FHD_INT_GET_BYTE(C, Byte)                     \
    do {                                                \
      CorSave (C);                                      \
      if ((C)->InputPlace >= (C)->InputLen) {           \
        return FHD_RET_END_OF_INPUT;                    \
      }                                                 \
      (Byte) = (C)->InputBuffer[(C)->InputPlace];       \
      (C)->InputPlace++;                                \
    } while (0)

#elif defined(FHD_INPUT_FUNC)
  // Input is a function

  #if defined (FHD_INPUT_MEMORY) || defined (FHD_INPUT_BUFFER) || defined (FHD_INPUT_PUTC) || defined (FHD_INPUT_CUSTOM)
  #error More than one FHD_INPUT_* macro is defined
  #endif

  #define FHD_INT_INPUT_TAG func
  #define FHD_INT_INPUT_OPAQUE

  #define FHD_INT_GET_BYTE(C, Byte)             \
    do {                                        \
      CorSave(C);                               \
      (Byte) = FHD_INPUT_FUNC(C);               \
      if ((Byte) < 0) {                         \
        return (Byte);                          \
      }                                         \
    } while (0)


#elif defined(FHD_INPUT_PUTC)
  // Input provided a byte at a time.  Note:  first byte is ignored.

  #if defined (FHD_INPUT_MEMORY) || defined (FHD_INPUT_BUFFER) || defined (FHD_INPUT_FUNC) || defined (FHD_INPUT_CUSTOM)
  #error More than one FHD_INPUT_* macro is defined
  #endif

  #define FHD_INT_INPUT_TAG putc
  #define FHD_INT_USE_COROUTINE
  #define FHD_INT_INPUT_OPAQUE                  \
     UINT8 InputByte;

  #define FHD_INT_GET_BYTE(C, Byte)             \
    do {                                        \
      CorReturn ((C), FHD_RET_END_OF_INPUT);    \
      (Byte) = (C)->InputByte;                  \
    } while (0)


#elif defined(FHD_INPUT_CUSTOM)
  // User-defined input code

  #if defined (FHD_INPUT_MEMORY) || defined (FHD_INPUT_BUFFER) || defined (FHD_INPUT_FUNC) || defined (FHD_INPUT_PUTC)
  #error More than one FHD_INPUT_* macro is defined
  #endif

  // Make sure the required macros are provided
  #ifndef FHD_INT_GET_BYTE
  #error Must define FHD_INT_GET_BYTE when using FHD_INPUT_CUSTOM
  #endif

  #if !defined(FHD_INT_INPUT_TAG) && !defined(FHD_INT_NAME)
  #error Must define FHD_INT_INPUT_TAG or FHD_INT_NAME
  #endif

#else // FHD_INPUT_*
#error Must define a FHD_INPUT_* macro before #including this file
#endif // FHD_INPUT_*


// Output

#if defined (FHD_OUTPUT_MEMORY)
  // Output is a single buffer in memory

  #if defined (FHD_OUTPUT_BUFFER) || defined (FHD_OUTPUT_FUNC) || defined (FHD_OUTPUT_GETC) || defined (FHD_OUTPUT_CUSTOM)
  #error More than one FHD_OUTPUT_* macro is defined
  #endif

  #define FHD_INT_OUTPUT_TAG memory

  // The output buffer can be used as the history buffer as well, so track the
  // buffer base address and offset separately.
  #define FHD_INT_OUTPUT_OPAQUE                 \
          UINT8 *OutputBuffer;                  \
          INTN  OutputLen;                      \
          INTN  OutputBufferPlace;

  #define FHD_INT_PUT_BYTE(C, Byte)                             \
    do {                                                        \
      if ((C)->OutputBufferPlace >= (C)->OutputLen) {           \
        return FHD_RET_OVERFLOW;                                \
      }                                                         \
      (C)->OutputBuffer[(C)->OutputBufferPlace++] = (Byte);     \
    } while (0)

  #define FHD_INT_HISTORY_OPAQUE          // No extra fields
  #define FHD_INT_ADD_TO_HISTORY(C, Byte) // Nothing to store
  #define FHD_INT_GET_HISTORY(C, Index, Out)                    \
    do {                                                        \
      INTN BufferPlace;                                         \
      BufferPlace = (C)->OutputBufferPlace - (Index);           \
      if (BufferPlace < 0)                                      \
        BufferPlace += (C)->OutputLen;                          \
      if (BufferPlace < 0)                                      \
        return -9; /* corrupt input */                          \
      (Out) = (C)->OutputBuffer[BufferPlace];                   \
    } while (0)

#elif defined(FHD_OUTPUT_BUFFER)
  // Output is a refillable buffer in memory

  #if defined (FHD_OUTPUT_MEMORY) || defined (FHD_OUTPUT_FUNC) || defined (FHD_OUTPUT_GETC) || defined (FHD_OUTPUT_CUSTOM)
  #error More than one FHD_OUTPUT_* macro is defined
  #endif

  #define FHD_INT_OUTPUT_TAG buffer
  #define FHD_INT_USE_COROUTINE
  #define FHD_INT_OUTPUT_OPAQUE                 \
          UINT8 *OutputBuffer;                  \
          INTN  OutputLen;                      \
          INTN  OutputPlace;                    \
          UINT8 OutputByte;

  // Save byte across CorReturn() calls, but optimize for the common case
  #define FHD_INT_PUT_BYTE(C, Byte)                     \
    do {                                                \
      if ((C)->OutputPlace < (C)->OutputLen) {          \
      (C)->OutputBuffer[(C)->OutputPlace++] = (Byte);   \
        break;                                          \
      }                                                 \
      (C)->OutputByte = (Byte);                         \
      CorReturn (C, FHD_RET_OVERFLOW);                  \
      (Byte) = (C)->OutputByte;                         \
    } while (1)


#elif defined(FHD_OUTPUT_FUNC)
  // Output is a function

  #if defined (FHD_OUTPUT_MEMORY) || defined (FHD_OUTPUT_BUFFER) || defined (FHD_OUTPUT_GETC) || defined (FHD_OUTPUT_CUSTOM)
  #error More than one FHD_OUTPUT_* macro is defined
  #endif

  #define FHD_INT_OUTPUT_TAG func

  #ifdef FHD_USE_COROUTINE
  // If the user wants restartability, we need to save the output byte in the context.
  // Optimize the code flow for the successful case.
  #define FHD_INT_OUTPUT_OPAQUE \
    UINT8 OutputByte;
  #define FHD_INT_USE_COROUTINE
  #define FHD_INT_PUT_BYTE(C, Byte)             \
    do {                                        \
      INTN Ret;                                 \
      Ret = FHD_OUTPUT_FUNC((C), (Byte));       \
      if (Ret == FHD_RET_SUCCESS) {             \
        break;                                  \
      }                                         \
      (C)->OutputByte = (Byte);                 \
      CorReturn((C), Ret);                      \
      (Byte) = (C)->OutputByte;                 \
    } while (1)

  #else // FHD_USE_COROUTINE
  // If we don't need restartability, simplify the code.
  #define FHD_INT_OUTPUT_OPAQUE
  #define FHD_INT_PUT_BYTE(C, Byte)             \
    do {                                        \
      INTN Ret;                                 \
      Ret = FHD_OUTPUT_FUNC((C), (Byte));       \
      if (Ret != FHD_RET_SUCCESS) {             \
        return Ret;                             \
      }                                         \
    } while (0)
  #endif // FHD_USE_COROUTINE


#elif defined(FHD_OUTPUT_GETC)
  // Output returned a byte at a time from the decoding function

  #if defined (FHD_OUTPUT_MEMORY) || defined (FHD_OUTPUT_BUFFER) || defined (FHD_OUTPUT_FUNC) || defined (FHD_OUTPUT_CUSTOM)
  #error More than one FHD_OUTPUT_* macro is defined
  #endif

  #define FHD_INT_OUTPUT_TAG getc
  #define FHD_INT_USE_COROUTINE // Save state between calls
  #define FHD_INT_OUTPUT_OPAQUE // No extra context fields

  // Return the byte to the caller, saving state so the next call
  // resumes where this one left off.
  #define FHD_INT_PUT_BYTE(C, Byte)  CorReturn (C, Byte)

#elif defined(FHD_OUTPUT_CUSTOM)
  // User-defined output code

  #if defined (FHD_OUTPUT_MEMORY) || defined (FHD_OUTPUT_BUFFER) || defined (FHD_OUTPUT_FUNC) || defined (FHD_OUTPUT_GETC)
  #error More than one FHD_OUTPUT_* macro is defined
  #endif

  // Make sure the required macros are provided
  #ifndef FHD_INT_PUT_BYTE
  #error Must define FHD_INT_PUT_BYTE when using FHD_OUTPUT_CUSTOM
  #endif

  #if !defined(FHD_INT_OUTPUT_TAG) && !defined(FHD_INT_NAME)
  #error Must define FHD_INT_OUTPUT_TAG or FHD_INT_NAME
  #endif

#else // FHD_OUTPUT_*
#error Must define a FHD_OUTPUT_* macro before #including this file
#endif // FHD_OUTPUT_*


// History

// Allow user overrides
#ifdef FHD_HISTORY_USER_OPAQUE
#define FHD_INT_HISTORY_OPAQUE FHD_HISTORY_USER_OPAQUE
#endif // FHD_HISTORY_USER_OPAQUE

#ifdef FHD_HISTORY_USER_GET
#define FHD_INT_GET_HISTORY(C, Index, out) FHD_HISTORY_USER_GET((C), (Index), (out))
#endif //FHD_HISTORY_GET

#ifdef FHD_HISTORY_USER_ADD
#define FHD_INT_ADD_TO_HISTORY(C, Byte) FHD_HISTORY_USER_ADD((C), (Byte))
#endif // FHD_HISTORY_ADD


// Default:  use a separate history buffer directly in the context structure

#ifndef FHD_INT_HISTORY_OPAQUE
#define FHD_INT_HISTORY_OPAQUE                          \
  UINT8  HistoryBuffer[1 << FHD_INT_MAX_WINDOW];        \
  UINT16 HistoryBufferPlace;
#endif // FHD_INT_HISTORY_OPAQUE

#ifndef FHD_INT_GET_HISTORY
#define FHD_INT_GET_HISTORY(C, Index, out)                      \
  do {                                                          \
    INTN BufferPlace;                                           \
    BufferPlace = (C)->HistoryBufferPlace - (Index);            \
    if( BufferPlace < 0 )                                       \
      BufferPlace += sizeof((C)->HistoryBuffer);                \
    if( BufferPlace < 0 )                                       \
      return -9; /* corrupt input */                            \
    (out) = (C)->HistoryBuffer[BufferPlace];                    \
  } while (0)
#endif // FHD_INT_GET_HISTORY

#ifndef FHD_INT_ADD_TO_HISTORY
#define FHD_INT_ADD_TO_HISTORY(C, Byte)                                 \
  do {                                                                  \
    (C)->HistoryBuffer[(C)->HistoryBufferPlace++] = Byte;               \
    if ((C)->HistoryBufferPlace >= sizeof((C)->HistoryBuffer)) {        \
      (C)->HistoryBufferPlace = 0;                                      \
    }                                                                   \
  } while (0)
#endif // FHD_INT_ADD_TO_HISTORY

// Minimum history buffer size for the given number of history window bits
#define FHD_HISTORY_BUFFER_SIZE(bits) (1 << (bits))


// History window index bits

// The user can specify a fixed window bit count.  Use it for the max bits as well.
#ifdef FHD_FIXED_WINDOW
#if (FHD_FIXED_WINDOW > HEATSHRINK_MAX_WINDOW_BITS || FHD_FIXED_WINDOW < HEATSHRINK_MIN_WINDOW_BITS)
#error Bad FHD_FIXED_WINDOW value
#endif
#ifdef FHD_USER_WINDOW
#error Can only specify one of FHD_FIXED_WINDOW and FHD_USER_WINDOW
#endif

#define FHD_INT_WINDOW(C) FHD_FIXED_WINDOW
#define FHD_INT_MAX_WINDOW FHD_FIXED_WINDOW

// The user can calculate the window bits some special way
#elif defined(FHD_USER_WINDOW)
#define FHD_INT_WINDOW(C) FHD_USER_WINDOW(C)
#endif // FHD_FIXED/USER_WINDOW

#ifdef FHD_WINDOW_USER_OPAQUE
#define FHD_INT_WINDOW_OPAQUE FHD_WINDOW_USER_OPAQUE
#endif

// Default:  store the window bits in the context structure
#ifndef FHD_INT_WINDOW
#define FHD_INT_WINDOW(C) ((C)->WindowBits)
#define FHD_INT_WINDOW_OPAQUE UINT8 WindowBits;
#endif

#ifndef FHD_INT_WINDOW_OPAQUE
#define FHD_INT_WINDOW_OPAQUE
#endif


// Maximum window bits.  This determines the required history buffer size,
// and whether one or two bytes of backref index need to be fetched.
#ifdef FHD_MAX_WINDOW
#define FHD_INT_MAX_WINDOW FHD_MAX_WINDOW
#endif

// Default:  support the maximum possible window bits
#ifndef FHD_INT_MAX_WINDOW
#define FHD_INT_MAX_WINDOW HEATSHRINK_MAX_WINDOW_BITS
#endif



// Backreference length bits

// The user can specify a fixed backreference bit count.  Use it for the max
// bits as well.  The number of backreference bits can't exceed the number of
// window bits.
#ifdef FHD_FIXED_BACKREF
#if (FHD_FIXED_BACKREF < HEATSHRINK_MIN_LOOKAHEAD_BITS || FHD_FIXED_BACKREF > FHD_INT_MAX_WINDOW)
#error Bad FHD_FIXED_BACKREF value
#endif
#ifdef FHD_USER_BACKREF
#error Can only specify one of FHD_FIXED_BACKREF and FHD_USER_BACKREF
#endif

#define FHD_INT_BACKREF(C) FHD_FIXED_BACKREF
#define FHD_INT_MAX_BACKREF FHD_FIXED_BACKREF

// The user can calculate the backref bits some special way
#elif defined(FHD_USER_BACKREF)
#define FHD_INT_BACKREF(C) FHD_USER_BACKREF(C)
#endif // FHD_FIXED/USER_BACKREF

#ifdef FHD_BACKREF_USER_OPAQUE
#define FHD_INT_BACKREF_OPAQUE FHD_BACKREF_USER_OPAQUE
#endif

// Default:  store the backref bits in the context structure
#ifndef FHD_INT_BACKREF
#define FHD_INT_BACKREF(C) ((C)->BackrefBits)
#define FHD_INT_BACKREF_OPAQUE UINT8 BackrefBits;
#endif

#ifndef FHD_INT_BACKREF_OPAQUE
#define FHD_INT_BACKREF_OPAQUE
#endif


// Maximum backref bits.  This determines whether one or two bytes of backref
// index need to be fetched.
#ifdef FHD_MAX_BACKREF
#define FHD_INT_MAX_BACKREF FHD_MAX_BACKREF
#endif

// Default:  match the maximum window bits
#ifndef FHD_INT_MAX_BACKREF
#define FHD_INT_MAX_BACKREF FHD_INT_MAX_WINDOW
#endif


// Coroutines.  The user can force coroutines to be enabled.

#ifdef FHD_USE_COROUTINE
#define FHD_INT_USE_COROUTINE
#endif


// The user can supply extra context fields

#ifdef FHD_OPAQUE
#define FHD_INT_OPAQUE FHD_OPAQUE
#else
#define FHD_INT_OPAQUE
#endif


// Integer types

#ifndef FHD_NO_STDINT
#include <stdint.h>
typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef int      INTN;
#endif // FHD_NO_STDINT


// Constant types
#ifndef FHD_NO_CONST
#ifndef CONST
#define CONST const
#endif // CONST
#endif // FHD_NO_CONST

//
// End of configuration processing
//


// Return codes

#define FHD_RET_SUCCESS      -1 // Successful decompression
#define FHD_RET_END_OF_INPUT -2 // Input buffer is empty
#define FHD_RET_OVERFLOW     -3 // Output buffer is full


// Generate names for the context structure and decompression routine.
// (Requires two layers of macros, due to C's odd '##' semantics.)
#ifndef FHD_INT_NAME
#define FHD_INT_ARG3(a, b, c) a ## _ ## b ## _ ## c
#define FHD_INT_PASTE3(a, b, c) FHD_INT_ARG3(a, b, c)
#define FHD_INT_NAME(name) FHD_INT_PASTE3(name, FHD_INT_INPUT_TAG, FHD_INT_OUTPUT_TAG)
#endif // FHD_INT_NAME


// Use coroutines if neceessary.  Otherwise stub them out.
// See the design discussion here:
//   https://www.chiark.greenend.org.uk/~sgtatham/coroutines.html
#ifdef FHD_INT_USE_COROUTINE
#define CorDecl         UINT16 CorLine;
#define CorBegin(C)     switch ((C)->CorLine) { case 0:;
#define CorReturn(C, Val)                       \
  do {                                          \
    (C)->CorLine = __LINE__;                    \
    return (Val);                               \
    case __LINE__:;                             \
    } while (0)
#define CorSave(C)                              \
  do {                                          \
    (C)->CorLine = __LINE__;                    \
    case __LINE__:;                             \
    } while (0)
#define CorEnd }

#else // FHD_INT_USE_COROUTINE
#define CorDecl
#define CorBegin(C)
#define CorReturn(C, Val)
#define CorSave(C)
#define CorEnd

#endif // FHD_INT_USE_COROUTINE


// Decoration added to each declaration.  Defaults to empty.
#if defined(FHD_DECORATOR)
#define FHD_INT_DECORATOR FHD_DECORATOR
#endif

#ifndef FHD_INT_DECORATOR
#define FHD_INT_DECORATOR
#endif


// Declarations

// Context structure, holding the decompression state.  Pass a pointer
// to it into the decompressor.  The FHD_INT*_OPAQUE fields are
// #defined by the configuration section above.
typedef struct FHD_INT_NAME(fhd_context_s)
{
  CorDecl               // State for coroutine macros
  UINT16 Index;         // How far back to look to copy a backreference
  UINT16 count;         // How many backreference bytes to copy
  UINT8  BitIndex;      // Bit position in current input byte
  UINT8  CurrentByte;   // Current input byte
  FHD_INT_WINDOW_OPAQUE
  FHD_INT_BACKREF_OPAQUE
  FHD_INT_HISTORY_OPAQUE
  FHD_INT_INPUT_OPAQUE
  FHD_INT_OUTPUT_OPAQUE
  // Caller-supplied opaque data
  FHD_INT_OPAQUE
} FHD_INT_NAME(fhd_context);

FHD_INT_DECORATOR INTN FHD_INT_NAME(fhd) (FHD_INT_NAME(fhd_context) *C);


#ifdef FHD_IMPLEMENTATION

// Internal helper function:  set Out to the next Count bits from the input
// (Count <= 8).  A macro, so the coroutine macros based on __LINE__ can save
// a unique line number in the context structure.
#define GETBITS(Out, C, Count)                                  \
    do {                                                        \
      INTN NextIndex;                                           \
      INTN BytePair;                                            \
      INTN NextByte;                                            \
      if ((Count) > (C)->BitIndex) {                            \
        FHD_INT_GET_BYTE ((C), NextByte);                       \
        BytePair = ( (C)->CurrentByte << 8 ) | NextByte;        \
        NextIndex = (C)->BitIndex - (Count) + 8;                \
        (C)->CurrentByte = NextByte;                            \
      }                                                         \
      else {                                                    \
        BytePair = (C)->CurrentByte;                            \
        NextIndex = (C)->BitIndex - (Count);                    \
      }                                                         \
      (Out) = (BytePair >> NextIndex) & ((1 << (Count)) - 1);   \
      (C)->BitIndex = NextIndex;                                \
    } while (0)


/**
  Decompress heatshrink-encoded data.

  Decompress, according to the context structure C.  Return an error/status code or other
  data.  The details of the context structure and return values vary depending on the
  configuration macros defined above.

  @param   C       Context structure, holding information about the input and output
                   flows and the state of the decompressor.

  @return  Status  A decompressed character, or a negative status code (FHD_RET_* or
                   other code, depending on configuration.)

**/
FHD_INT_DECORATOR INTN FHD_INT_NAME(fhd) (FHD_INT_NAME(fhd_context) *C)
{
  INTN Bits;
  CorBegin(C);

  // Here's the decompression algorithm in a nutshell:
  //
  // while (not end of input):
  //   Read a tag bit
  //   If it's a backref marker:
  //     read <window size> bits to get the index
  //     read <count size> bits to get the count
  //     while count > 0:
  //       go back index bytes in the history buffer to read a character
  //       write it to the history buffer
  //       emit it
  //       count--
  //   else, i.e. not a backref marker:
  //     read an 8-bit literal
  //     write it to the history buffer
  //     emit it
  //
  // We implement this flow using the "Cor" macros, so we don't have to
  // maintain an explicit state machine.  Each call to this routine will
  // resume at the point it previously returned.

  for (;;) {

    // Read a tag bit
    GETBITS(Bits, C, 1);

    if (Bits != HEATSHRINK_BACKREF_MARKER) {
      // Not a backref, so it's an 8-bit literal
      GETBITS(Bits, C, 8);
      FHD_INT_ADD_TO_HISTORY (C, Bits);
      FHD_INT_PUT_BYTE(C, Bits);
    }
    else {
      // A backreference.  Read the index
#if (FHD_INT_MAX_WINDOW > 8)
      if (FHD_INT_WINDOW(C) > 8) {
        // Two bytes needed.  Read the upper bits, then the lower bits
        GETBITS(Bits, C, FHD_INT_WINDOW(C) - 8);
        C->Index = Bits << 8;
        GETBITS(Bits, C, 8);
        C->Index |= Bits;
      }
      else
#endif // FHD_INT_MAX_WINDOW > 8
        {
          // One byte needed.
          GETBITS(Bits, C, FHD_INT_WINDOW(C));
          C->Index = Bits;
        }
      // The index is encoded as 1 less than the number of characters
      // to look back.  This makes an encoding of 0 meaningful.
      C->Index += 1;

      // Read the count
#if (FHD_INT_MAX_BACKREF > 8)
      if (FHD_INT_BACKREF(C) > 8) {
        // Two bytes needed.  Read the upper bits, then the lower bits
        GETBITS(Bits, C, FHD_INT_BACKREF(C) - 8);
        C->count = Bits << 8;
        GETBITS(Bits, C, 8);
        C->count |= Bits;
      }
      else
#endif // FHD_INT_MAX_BACKREF
        {
          // One byte needed.
          GETBITS(Bits, C, FHD_INT_BACKREF(C));
          C->count = Bits;
        }
      // The count is also encoded as 1 less than the number of characters
      // to copy.  This is handled by the organization of the loop below.

      // Copy count+1 bytes from the history buffer.
      do {
        FHD_INT_GET_HISTORY(C, C->Index, Bits);
        FHD_INT_ADD_TO_HISTORY(C, Bits);
        FHD_INT_PUT_BYTE(C, Bits);
      } while (C->count-- > 0);
    } // HEATSHRINK_BACKREF_MARKER

  } // for (;;)

  CorEnd

  return FHD_RET_SUCCESS;
}

#undef GETBITS

#endif // FHD_IMPLEMENTATION


// Clean up.  Undefine all internal macros, so this file can be #included again
// with a different configuration.

#undef FHD_INT_INPUT_OPAQUE
#undef FHD_INT_GET_BYTE
#undef FHD_INT_INPUT_TAG
#undef FHD_INT_OUTPUT_OPAQUE
#undef FHD_INT_PUT_BYTE
#undef FHD_INT_OUTPUT_TAG
#undef FHD_INT_HISTORY_OPAQUE
#undef FHD_INT_ADD_TO_HISTORY
#undef FHD_INT_GET_HISTORY
#undef FHD_INT_USE_COROUTINE
#undef FHD_INT_NAME
#undef FHD_INT_WINDOW
#undef FHD_INT_MAX_WINDOW
#undef FHD_INT_WINDOW_OPAQUE
#undef FHD_INT_BACKREF
#undef FHD_INT_MAX_BACKREF
#undef FHD_INT_BACKREF_OPAQUE
#undef FHD_INT_OPAQUE
#undef FHD_INT_DECORATOR
#undef CorDecl
#undef CorBegin
#undef CorReturn
#undef CorSave
#undef CorEnd
