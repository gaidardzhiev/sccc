sccc
====

## NAME

sccc - self compiling C compiler for x86-64

## SYNOPSIS

```
sccc [-o out] [-I dir] [-D name[=value]] [-nostdlib] file.c ...
sccc -ffreestanding [-mgeneral-regs-only] [-e sym] [-Ttext addr]
     [-oformat elf|bin|img|multiboot] [-o out] file.c ...
```

## DESCRIPTION

*sccc* is a C compiler for x86-64, written entirely in C99 and capable of compiling itself. It takes ordinary C source and writes Linux executables directly, complete with its own C library baked in, so a program goes from source to a running binary in one step. Point it at kernel code instead and it writes a flat binary, a bootable disk image, or a multiboot image you can run in QEMU or Bochs.

## BUILDING

A C99 compiler is needed for the first build. The Makefile uses gcc.

```
make
```

builds *sccc*.

```
make bootstrap
```

compiles *sccc.c* with *sccc* to make *sccc2*, compiles it again with *sccc2* to make *sccc3*, and compares the two. They are identical. A self compile takes a few hundredths of a second.

Once *sccc* exists it can rebuild itself without gcc:

```
./sccc sccc.c -o sccc
```

The old output file is removed before the new one is written, so a running *sccc* may replace itself.

## INSTALLATION

The binary is self-contained. Copy it to a directory in the search path:

```
install -m 755 sccc /usr/local/bin
```

There is no install target and nothing else to install.

## OPTIONS

```
-o out           Write the output to out.
-I dir           Search dir for included files.  The directory may
                 follow the option or be attached to it.
-D name[=value]  Define name as a macro.  Without a value it is
                 defined as 1.
-nostdlib        Leave out the built-in library.  The value returned
                 by main is passed straight to the exit system call,
                 and the program reaches the kernel with __syscall.
-ffreestanding   Compile a kernel or other bare metal program.  The
                 built-in library and the startup code are left
                 out, main is optional, and __STDC_HOSTED__ is 0.
                 The entry point is _start unless -e is given.
-e sym           Make the function sym the entry point.
-Ttext addr      Place .text at addr.  .rodata follows it, and .data
                 and .bss follow on the next page, or on the next 16
                 bytes in a flat binary.  addr may be decimal, octal,
                 or hexadecimal.
-oformat fmt     Write the output as elf, bin, img, or multiboot.
                 The default is elf.  See OUTPUT FORMATS.
-mgeneral-regs-only
                 Make float and double an error, so the program
                 uses only the general registers.  Use it with
                 -nostdlib or -ffreestanding.
```

Any other argument beginning with a minus sign is an error.

## INCLUDE FILES

An absolute path is used as given. Otherwise `#include "file"` searches the directory of the including file, then each *-I* directory in order, then the built-in headers. `#include <file>` skips the directory of the including file. Files in */usr/include* are never read.

## EXIT STATUS

*sccc* exits 0 when it has written the output file and 1 otherwise. It fails when there is no input file, when an option is unknown, when an input file cannot be read, on a compile error, when `main` or the entry point is not defined, when a kernel does not fit its output format, and when the output file cannot be written.

## DIAGNOSTICS

Errors have the form

```
file:line: error: message
```

The first error stops compilation. Warnings have the same form with `warning:` and do not. A program without `main` gets

```
error: undefined reference to 'main'
```

## LANGUAGE

*sccc* accepts C99 except for the items listed under BUGS. This includes all integer types, `_Bool`, `float`, `double`, pointers, arrays, structures, unions, enumerations, typedefs, bit fields, function pointers, anonymous structure and union members, and flexible array members. Constants may be decimal, octal, hexadecimal, character, or floating, and floating constants may be hexadecimal. Initializers may be nested and may use designators such as `.f`, `[i]` and `[2].x`, brace elision, string literals, and compound literals. Static data may hold address constants. All statements are accepted, including `goto` and `switch` cases inside loops. Variadic functions use `<stdarg.h>`. `__func__`, `_Static_assert`, `inline`, and `restrict` are accepted.

The builtin `__syscall(nr, ...)` makes Linux system call `nr` with up to six arguments and yields the raw result.

The preprocessor handles object-like and function-like macros, `#`, `##`, `__VA_ARGS__`, the GNU form `, ## __VA_ARGS__`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, `defined`, `#undef`, `#error`, `#warning`, `#line`, and `#pragma once`. Other pragmas are ignored. Macro arguments are expanded before substitution. The predefined macros are `__STDC__`, `__STDC_VERSION__` (199901L), `__STDC_HOSTED__` (1, or 0 with *-ffreestanding*), `__x86_64__`, `__x86_64`, `__amd64__`, `__linux__`, `__linux`, `__unix__`, `__unix`, `__LP64__`, `_LP64`, `__ELF__`, `__CHAR_BIT__`, `__SIZEOF_INT__`, `__SIZEOF_LONG__`, `__SIZEOF_LONG_LONG__`, `__SIZEOF_SHORT__`, `__SIZEOF_POINTER__`, `__FILE__`, and `__LINE__`. `__extension__` expands to nothing.

`__attribute__((packed))` on a structure or union gives each member an alignment of 1, and bit fields are packed bit by bit, as gcc packs them. The attribute goes after `struct` or `union`, or after the closing brace. `__attribute__((interrupt))` makes a function an interrupt handler, as described under INTERRUPT HANDLERS. Other attributes are accepted and ignored.

## LIBRARY

Unless *-nostdlib* is given, the built-in library is compiled into each executable. A function defined by the program replaces the library function of the same name.

The headers are *assert.h*, *ctype.h*, *errno.h*, *fcntl.h*, *float.h*, *inttypes.h*, *limits.h*, *math.h*, *stdarg.h*, *stdbool.h*, *stddef.h*, *stdint.h*, *stdio.h*, *stdlib.h*, *string.h*, *unistd.h*, *sys/mman.h*, *sys/stat.h*, and *sys/types.h*. They declare these functions:

```
stdio.h    fopen fdopen fclose fflush setvbuf setbuf fread fwrite
           fseek ftell rewind fgetc getc getchar ungetc fgets fputc
           putc putchar fputs puts printf fprintf sprintf snprintf
           vprintf vfprintf vsprintf vsnprintf feof ferror clearerr
           fileno perror remove rename
stdlib.h   malloc calloc realloc free exit atexit abort getenv atoi
           atol atoll atof strtol strtoll strtoul strtoull strtod
           strtof abs labs llabs qsort bsearch rand srand
string.h   memcpy memmove memset memcmp memchr strlen strnlen strcpy
           strncpy strcat strncat strcmp strncmp strchr strrchr strstr
           strspn strcspn strpbrk strtok strdup strndup strerror
ctype.h    isalnum isalpha isblank iscntrl isdigit isgraph islower
           isprint ispunct isspace isupper isxdigit tolower toupper
math.h     sqrt fabs fabsf floor ceil round trunc fmod modf frexp
           ldexp exp log log10 pow sin cos tan atan atan2 isnan isinf
           isfinite
unistd.h   read write close lseek unlink access getpid isatty getcwd
           _exit
fcntl.h    open
sys/stat.h stat fstat chmod mkdir
sys/mman.h mmap munmap
```

Standard error is unbuffered. Standard output is line buffered when it is a terminal and fully buffered otherwise. Other streams are fully buffered. Line buffered streams are flushed before input is read, so a prompt appears before the program waits. `setvbuf` changes the mode. All streams are flushed by `exit` and on return from `main`.

`strtod` is correctly rounded. It accepts decimal and hexadecimal numbers, `inf`, `infinity`, and `nan`. `printf` prints floating values with the same digits as glibc. The one known difference is `%#g` when rounding raises the exponent: *sccc* keeps the trailing zeros, as the standard requires, and glibc drops them.

`malloc` gets memory from the kernel with `mmap`.

## MACHINE BUILTINS

These builtins compile to single instructions or short sequences, for kernels and drivers. Arguments are integers or pointers.

```
__outb(port, v)  __outw(port, v)  __outl(port, v)
                         write v to an I/O port
__inb(port)  __inw(port)  __inl(port)
                         read an I/O port, as unsigned char,
                         unsigned short, or unsigned int
__cli()  __sti()  __hlt()  __pause()
__lgdt(p)  __lidt(p)     load GDTR or IDTR from the 10-byte
                         descriptor at p
__ltr(sel)               load the task register
__read_cr0()  __read_cr2()  __read_cr3()  __read_cr4()
                         read a control register, as unsigned long
__write_cr0(v)  __write_cr2(v)  __write_cr3(v)  __write_cr4(v)
__rdmsr(msr)             read a model specific register, as
                         unsigned long
__wrmsr(msr, v)          write a model specific register
__invlpg(p)              flush the TLB entry for address p
__cpuid(leaf, sub, r)    store eax, ebx, ecx, and edx in
                         unsigned r[4]
__rdtsc()                read the time stamp counter, as
                         unsigned long
```

## INTERRUPT HANDLERS

A function with `__attribute__((interrupt))` goes straight into an IDT gate. It has one of two forms:

```
struct frame { unsigned long ip, cs, flags, sp, ss; };

__attribute__((interrupt)) void handler(struct frame *f);
__attribute__((interrupt)) void handler(struct frame *f, unsigned long error);
```

The second form is for exceptions that push an error code, such as #GP and #PF. The handler clears the direction flag, saves `rax`, `rcx`, `rdx`, `rsi`, `rdi`, and `r8` to `r11`, calls the body with the stack aligned to 16 bytes, restores the registers, drops the error code, and returns with `iretq`. Writing to `*f` changes where the interrupted code resumes. Handlers are compiled with *-mgeneral-regs-only*.

## OUTPUT FORMATS

**elf** is the default. A hosted program is the executable described under IMPLEMENTATION. With *-ffreestanding* the file has no startup code, and its entry address is the entry point. *-Ttext* moves the whole program.

**bin** is a flat binary: `.text`, `.rodata`, and `.data`, back to back. It is loaded at the *-Ttext* address, 0 by default, and execution starts at its first byte, so the entry point is the first function in the first file. `.bss` follows `.data` in memory.

**img** is a raw disk image for a PC BIOS. Sector 0 is a boot sector written by *sccc*, and the kernel, as a flat binary, starts at sector 1. The boot sector

1. reads the BIOS memory map (E820) to 0x5000: a 32-bit count, then entries of 24 bytes (base, length, type, extended attributes) from 0x5008,
2. loads the kernel to 0x10000 with `int 13h`,
3. enables the A20 line,
4. identity maps the first 1 GB with 2 MB pages, using page tables at 0x1000 to 0x3fff,
5. switches to 64-bit long mode,
6. copies the kernel to its *-Ttext* address, 0x100000 by default, and clears its `.bss`,
7. calls the entry point with `rdi` = 0x5000 and `rsp` = 0x80000, with interrupts disabled.

The kernel loads between 1 MB and 1 GB and is at most 588800 bytes. The image is padded to whole cylinders of 16 heads and 63 sectors, 516096 bytes each.

**multiboot** is a Multiboot 1 kernel for GRUB and for `qemu -kernel`. Its first page holds the Multiboot header, with load addresses, and a trampoline from 32-bit protected mode to long mode. The kernel follows at the *-Ttext* address, 0x101000 by default, which must be page aligned. The trampoline identity maps the first 1 GB and calls the entry point with `rdi` = the address of the Multiboot information structure, `rsi` = 0x2BADB002, and a 16 KB stack. The page tables and the stack lie after `.bss`.

## RUNNING A KERNEL

Build a kernel from *baremetal/* as a disk image:

```
./sccc -ffreestanding -mgeneral-regs-only -oformat img -e kmain baremetal/hello.c -o hello.img
```

Boot it in QEMU. The VGA text appears in the window and the serial output in the terminal:

```
qemu-system-x86_64 -drive file=hello.img,format=raw -serial stdio
```

To run without a window and stop when the kernel finishes, add the debug exit device. The *baremetal/* kernels write 0x10 to port 0xf4 at the end, and QEMU exits with status 33:

```
qemu-system-x86_64 -display none -serial stdio \
    -device isa-debug-exit,iobase=0xf4,iosize=4 \
    -drive file=hello.img,format=raw
```

The same kernel as a Multiboot image:

```
./sccc -ffreestanding -mgeneral-regs-only -oformat multiboot -e kmain baremetal/hello.c -o hello.mb
qemu-system-x86_64 -kernel hello.mb -serial stdio
```

For Bochs, write a configuration file and start it:

```
cat > bochsrc <<EOF
megs: 64
cpu: model=corei7_sandy_bridge_2600k
romimage: file=/usr/share/bochs/BIOS-bochs-latest
vgaromimage: file=/usr/share/bochs/VGABIOS-lgpl-latest.bin
ata0-master: type=disk, path=hello.img, mode=flat
boot: disk
com1: enabled=1, mode=file, dev=serial.txt
EOF
bochs -q -f bochsrc
cat serial.txt
```

The kernel stops Bochs through port 0x8900 when it finishes. To run Bochs without a window, add `display_library: nogui` to *bochsrc*. Where Bochs is built without *nogui*, add `display_library: sdl2` instead and run

```
SDL_VIDEODRIVER=dummy bochs -q -f bochsrc
```

*keyboard.c* echoes the keys typed in the QEMU or Bochs window to the serial port, until Enter.

## IMPLEMENTATION

*sccc* makes one pass over the source. The recursive descent parser emits machine code as it goes. There is no syntax tree and no intermediate form.

Code generation uses an accumulator. Every expression leaves its value in `rax`. Temporaries are pushed on the stack, and the pushes are counted so that calls are made with the stack aligned to 16 bytes. For an lvalue `rax` holds the address, and the value is loaded only when it is used. Arrays and functions decay at that point. Jumps to code not yet emitted, from `break`, `continue`, `goto` and `switch`, are patched when the target is known. `sizeof expr` compiles its operand and then discards the code. `float` and `double` values are kept in `rax` as raw bits, and SSE2 instructions do the arithmetic, comparisons, and conversions.

A hosted program is a position dependent executable (`ET_EXEC`) loaded at 0x400000, with two loadable segments:

| Segment | Flags | Sections                                    |
|---------|-------|---------------------------------------------|
| 1       | R X   | ELF and program headers, .text, .rodata     |
| 2       | R W   | .data, .bss                                 |

The file also has `.symtab`, `.strtab`, and `.shstrtab`, so *readelf*, *objdump*, and *gdb* can read it. *sccc* resolves all relocations itself, and the file has none. The entry point `_start` sets up `argc`, `argv`, and `environ`, initializes standard I/O, calls `main`, and passes its value to `exit`. A hello world program is about 84 KB, or about 5 KB with *-nostdlib*.

Arguments are passed in `rdi`, `rsi`, `rdx`, `rcx`, `r8`, and `r9`, then on the stack. Results return in `rax`. Floating arguments and results use the same integer registers. A structure argument is passed as a pointer, and the callee copies it. A function returning a structure gets a hidden pointer in `rdi`, and the other arguments move up one register. `va_list` has the System V layout.

The headers and library live in *lib/*. *tools/embed.sh* turns them into C string tables in *builtin.h*, which *sccc.c* includes. The library is split into strings of 40 lines, each shorter than the 4095 bytes ISO C allows. After a change in *lib/*, run

```
make embed
make
```

## TESTING

```
make check
```

runs `make bootstrap`, each suite below with both *sccc* and *sccc2*, and the bare metal tests with *sccc*.

*test_sccc.c* is a harness of 96 cases. It writes each program to */tmp/tc_src.c*, compiles it, runs it, and checks the exit status. Four of the cases must fail to compile. Run it by hand with

```
make test
./test_sccc ./sccc
```

*tests/run.sh* compiles and runs each program in *tests/*. A test passes if it exits 0 and, when *NAME.out* exists, its standard output and standard error match that file. Tests without an output file check their own results and report through the exit status. There are 16 tests. The *.out* files were produced by gcc. *tests/gcccheck.sh* builds the same tests with gcc to check the tests themselves.

*examples/run.sh* checks the example programs, as described below.

```
make baremetal
```

or `baremetal/run.sh [compiler]` builds each kernel in *baremetal/* as a disk image and boots it in QEMU and in Bochs, without a window. A kernel passes when its serial output matches *NAME.out*, and in QEMU when it also exits through the debug exit device. The kernels are

```
hello       serial port and VGA text
exceptions  IDT, #BP, #DE, and #PF with an error code
timer       PIT interrupts through the PIC
keyboard    PS/2 keyboard interrupts and echo
memory      page frame allocator and heap over the E820 map
```

In QEMU the keyboard test types the keys in *keyboard.keys* through the monitor. In Bochs the kernel is built with `-DINJECT` and feeds the same scancodes through the keyboard controller. Each emulator runs under *timeout*, and under *systemd-run* with memory and time limits when it is available. An emulator that is not installed is skipped.

```
make fuzz
```

runs *tools/fuzz_expr.py* for 50 rounds. Each round generates a program of 200 random integer and floating expressions, builds it with *sccc* and with `gcc -fwrapv`, and compares the output. A mismatching program is saved as *fuzz_fail_N.c*. The environment variable `SEED0` sets the first seed.

## EXAMPLES

*examples/* holds 47 complete programs:

```
algorithms   fib primes queens hanoi sort bigfact pi sudoku knight
             dijkstra dp huffman maze tictactoe sha256 encode roman
data         list bst wordfreq matrix
interpreters calc rpn lisp vm brainf json grep tokens
language     structs funcptr varargs preproc bits goto floats strings
             modules
programs     hello args files hexdump wc cal guess life mandel
```

To build and run one:

```
./sccc examples/cal.c -o cal
./cal 2026 10
```

*modules* has two translation units:

```
./sccc examples/modules.c examples/modules_part2.c -o modules
```

*calc* and *guess* read standard input interactively. *primes*, *pi*, *cal*, *guess*, and *hexdump* take arguments.

```
make examples
```

or `examples/run.sh [compiler]` compiles each program with *compiler* and runs it in a temporary directory. The default compiler is the *sccc* in the directory above *examples/*. *NAME.in*, if present, is the standard input; otherwise it is */dev/null*. The words of *NAME.args*, if present, are the arguments. Files named *NAME_partN.c* are compiled with *NAME.c*. The program must exit 0, and its standard output must match *NAME.out*, which was produced by gcc. When *timeout* exists each program is limited to 10 seconds. `examples/run.sh compiler -update` rebuilds the *.out* files with gcc. The script exits nonzero if any program fails.

## FILES

```
sccc.c              the compiler
builtin.h           generated by make embed; do not edit
lib/include/        sources of the built-in headers
lib/libc.c          source of the built-in library
tools/embed.sh      writes builtin.h from lib/
tools/fuzz_expr.py  random expression tester
test_sccc.c         exit status test harness
tests/              language tests and expected output
examples/           example programs, inputs, and expected output
baremetal/          test kernels, expected serial output, run.sh
Makefile            build, bootstrap, and test targets
COPYING             the GNU General Public License, version 3
```

## SEE ALSO

gcc(1), elf(5), syscall(2), qemu-system-x86_64(1), bochs(1)

## BUGS

Variable length arrays are not supported.

K&R function definitions are not supported.

Wide characters and wide strings are not supported.

`_Complex`, `_Generic`, `_Alignas`, `_Alignof`, `_Noreturn`, `typeof`, and statement expressions are not supported.

Universal character names are not recognized. `'\u0041'` is accepted and is not `'A'`.

Trigraphs and digraphs are not recognized.

`__asm__(x)` expands to nothing, so inline assembly is dropped without a message.

`long double` is `double`.

The math functions are not correctly rounded. `sqrt` can be one unit in the last place off. `sin`, `cos`, and `log` have absolute errors of about 1e-15, so their relative error is large near their zeros.

`printf` has no `%a`.

There is no `scanf` family, and no `time.h`, `signal.h`, or `setjmp.h`.

`setvbuf` ignores its buffer argument.

The *-I* directories are also searched when the built-in library is compiled. A file there named like a built-in header replaces it for the library too, and the build fails.

Input files after the 64th are ignored without a message. A 65th *-I* or *-D* is reported as an unknown argument.

Floating values and structures are not passed as the System V ABI requires. Object files from other compilers cannot be linked in.

A bit field in a packed structure may span at most 8 bytes.

The only target is x86-64.

## LICENSE

Copyright (C) 2026 Ivan Gaydardzhiev. *sccc* is distributed under the GNU General Public License, version 3 only. See *COPYING*.
