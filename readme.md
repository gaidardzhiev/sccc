sccc
====

## NAME

sccc - self compiling C compiler for x86-64

## SYNOPSIS

```
sccc [-o out] [-I dir] [-D name[=value]] [-nostdlib] file ...
sccc -c [-o out] [-I dir] [-D name[=value]] file ...
sccc -ffreestanding [-mgeneral-regs-only] [-e sym] [-Ttext addr | -T script]
     [-oformat elf|bin|img|multiboot] [-o out] file ...
```

## DESCRIPTION

*sccc* is a C compiler for x86-64, written entirely in C99 and capable of compiling itself. It takes ordinary C source and writes Linux executables directly, complete with its own C library baked in, so a program goes from source to a running binary in one step. Point it at kernel code instead and it writes a flat binary, a bootable disk image, or a multiboot image you can run in QEMU or Bochs.

A file whose name ends in *.s* is assembly, and one ending in *.S* is assembly that goes through the preprocessor first. A file ending in *.o* is a relocatable object and one ending in *.a* an archive of them, which are linked in. Every other file is C. *sccc* assembles AT&T syntax itself, in 16-bit, 32-bit, and 64-bit code, and C and assembly files on one command line share their symbols.

With *-c* each C or assembly file becomes an object file instead, which *sccc* or another linker can link later. *sccc* follows the System V calling convention, so its objects and gcc's call each other in both directions. See OBJECT FILES AND LINKING. *-T* takes a linker script in the GNU ld language to control the layout of the output, as kernels need. See LINKER SCRIPTS.

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
-c               Write a relocatable object for each input file,
                 named after it with the suffix .o, in the current
                 directory, or out when there is one input.
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
-T script        Lay out the output with a GNU ld script, for a
                 link and for a one-step compile alike.  See LINKER
                 SCRIPTS.  It cannot be used with -c or -Ttext.
-oformat fmt     Write the output as elf, bin, img, or multiboot.
                 The default is elf, or what OUTPUT_FORMAT in the
                 script names.  See OUTPUT FORMATS.
-mgeneral-regs-only
                 Make float and double an error, so the program
                 uses only the general registers.  Use it with
                 -nostdlib or -ffreestanding.
```

Any other argument beginning with a minus sign is an error.

## OBJECT FILES AND LINKING

*sccc -c* writes an ELF relocatable object (`ET_REL`) with the sections the file uses (`.text`, `.rodata`, `.data`, `.bss`, and the named sections described under SECTIONS), a `.rela` section for each section with relocations, `.symtab`, `.strtab`, and an empty `.note.GNU-stack`. Calls use `R_X86_64_PLT32`, other relative references `R_X86_64_PC32`, and absolute ones `R_X86_64_64`, `32`, or `32S`; the assembler also writes the 16-bit and 8-bit forms. References to static symbols go through the section symbol. A file scope variable without an initializer is a common symbol, as with gcc `-fcommon`, so several files may define it. Objects are linked without the built-in library and without the startup code, which the link step adds.

Without *-c*, the inputs are linked in the order given: C and assembly files are compiled, objects are loaded, and an archive contributes only the members that define a symbol some earlier input references and nothing has defined yet, repeating within the archive until no more are needed. The built-in library comes last, unless *-nostdlib* is given, and a function in it gives way to one from the inputs. The linker resolves

- a symbol defined in one file and used in another;
- weak definitions, which give way to ordinary ones, and undefined weak references, which have the address 0;
- common symbols, which merge with each other and with an ordinary definition and otherwise get space in `.bss`;
- `__start_name` and `__stop_name`, as under SECTIONS;
- the GOT relocations of objects compiled with *-fPIC*, through a table in a section `.got`.

An input section named `.text`, `.rodata`, or `.data`, or of type `NOBITS`, joins the built-in section; any other keeps its name. `.eh_frame`, notes, and sections that are not loaded are dropped; with a linker script notes are kept, and every section keeps its name and file for the patterns of the script. Two definitions of one name are an error, as is a reference that nothing defines. In an object from *-c*, a named section of type `@nobits` is a section of its own, and a section whose name begins with `.note` has the type `SHT_NOTE`, as GNU as writes them.

gcc objects work when they do not need the C library of glibc. Compile them against the headers of *sccc* and without the stack protector, which reads a canary through `%fs`:

```
gcc -std=c99 -fno-pic -fno-stack-protector -nostdinc -I lib/include -c part.c
./sccc main.c part.o -o prog
```

gcc can also link objects from *sccc* with glibc, as a position dependent executable:

```
./sccc -c part.c
gcc -no-pie main.c part.o -o prog
```

## LINKER SCRIPTS

*-T script* reads a linker script in the GNU ld language. The inputs are compiled or loaded as usual, and the script decides where each of their sections goes. Every input section is placed whole: the `.text` of one C file, one section of an object, or a run of a named `@nobits` section in an assembly file. The supported subset:

```
SECTIONS { ... }    output sections, assignments, and the commands below
ENTRY(sym)          the entry point; -e takes precedence
OUTPUT_FORMAT(fmt)  elf64-x86-64 or binary; -oformat takes precedence
OUTPUT_ARCH(i386:x86-64)
PHDRS { name type [FILEHDR] [PHDRS] [AT(addr)] [FLAGS(n)]; ... }
SEARCH_DIR(dir)     accepted and ignored
sym = expr; . = expr;  and += -= *= /= <<= >>= &= |=
PROVIDE(sym = expr) PROVIDE_HIDDEN(sym = expr) HIDDEN(sym = expr)
ASSERT(expr, "message")
```

An output section is

```
name [address] [(NOLOAD)] : [AT(lma)] [ALIGN(n)]
{
    file(section pattern ...)    input sections, in input order
    KEEP(...)                    the same; nothing is collected
    SORT(...) SORT_BY_NAME(...) SORT_BY_ALIGNMENT(...)
    SORT_BY_INIT_PRIORITY(...) SORT_NONE(...)
    EXCLUDE_FILE(file ...)       before a file or a section pattern
    BYTE(e) SHORT(e) LONG(e) QUAD(e) SQUAD(e)
    FILL(e)                      the fill for the gaps after it
    sym = expr;  . = expr;  PROVIDE(...);  ASSERT(...)
} [:phdr ...] [=fill]
```

Patterns use `*`, `?`, and `[...]`. A file pattern matches the name given on the command line, or for an archive member the member's name; `archive:member` matches both, and `*(COMMON)` matches the common symbols. Each input section goes to the first statement that matches it, and `/DISCARD/` drops what it matches. A reference to a symbol in a dropped section is an error.

Expressions have the C operators, numbers in decimal, octal, and hex with the suffixes `K` and `M`, and `.`, `ALIGN(n)`, `ALIGN(e, n)`, `NEXT`, `ADDR`, `LOADADDR`, `SIZEOF`, `ALIGNOF`, `ABSOLUTE`, `DEFINED`, `MAX`, `MIN`, `LOG2CEIL`, `SIZEOF_HEADERS`, `CONSTANT(MAXPAGESIZE)` and `CONSTANT(COMMONPAGESIZE)` (both 4096), `DATA_SEGMENT_ALIGN`, `DATA_SEGMENT_END`, `DATA_SEGMENT_RELRO_END`, and `SEGMENT_START`. Values follow GNU ld: outside the output sections arithmetic is done on addresses, while inside one a number assigned to `.` or to a symbol is an offset from the start of the section. Forward references are allowed; the layout is repeated until no address changes.

The layout follows GNU ld. A section without an address starts at `.`, aligned to the largest alignment of its input. Without `AT`, a section keeps the difference between its address and its load address that the previous one had. A section with nothing in it is left out, but its symbols are still defined. Gaps in code are filled with `nop`s of up to 10 bytes, as GNU ld fills them. A section that no statement takes, an orphan, goes into the output section of its name, or into a new one after the section of its kind: code, read-only data, data, or zero-initialized data. Symbols `__start_name` and `__stop_name` are provided for output sections whose names are C identifiers.

The output is an ELF executable with a program header for each run of sections that share a page or follow each other with the same difference between address and load address, or with the headers of `PHDRS`; or a flat binary from the lowest load address, without the sections that are not loaded. *img* and *multiboot* take that flat binary. A higher-half kernel, linked at 0xffffffff80000000 above its load address, works with both: the boot code then also maps the first 1 GB there and jumps to the entry point at its high address, and the Multiboot header gives the physical addresses. A kernel with its own Multiboot header and boot code uses the binary format; in outline, *baremetal/higher/kernel.ld* is

```
OUTPUT_FORMAT(binary)
ENTRY(boot32)
KERNEL_VMA = 0xffffffff80000000;
SECTIONS
{
	. = 1M;
	.boot : { KEEP(*(.multiboot)) *(.boot.text) *(.boot.rodata) }
	. += KERNEL_VMA;
	.text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA) { *(.text .text.*) }
	.data : { *(.data) }
	.bss : { *(.bss) *(COMMON) }
	_kernel_end = .;
}
```

```
./sccc -ffreestanding -T kernel.ld kernel.c boot.S -o kernel.bin
qemu-system-x86_64 -kernel kernel.bin -serial stdio
```

## INCLUDE FILES

An absolute path is used as given. Otherwise `#include "file"` searches the directory of the including file, then each *-I* directory in order, then the built-in headers. `#include <file>` skips the directory of the including file. Files in */usr/include* are never read.

## EXIT STATUS

*sccc* exits 0 when it has written the output file and 1 otherwise. It fails when there is no input file, when an option is unknown, when an input file cannot be read, on a compile error, when an object or archive is malformed or uses an unsupported feature, on an undefined reference or a multiple definition, when `main` or the entry point is not defined, when a kernel does not fit its output format, and when the output file cannot be written.

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

From C11 come `_Generic`, `_Alignas`, `_Alignof`, and `_Noreturn`, with *stdalign.h* and *stdnoreturn.h*. `signed char` and `long long` are types of their own, distinct from `char` and `long`, as `_Generic` requires.

From GNU C come

- statement expressions, `({ int t = f(); t * t; })`, whose value is that of the last expression statement,
- `typeof`, `__typeof__`, and `__alignof__`,
- case ranges, `case 'a' ... 'z':`,
- range designators, `int t[16] = { [4 ... 7] = 1 };`,
- labels as values: `&&label` is the address of a label as `void *`, `goto *p;` jumps to it, and static arrays may hold label addresses, as in a threaded interpreter,
- zero-length arrays,
- inline assembly, as described under INLINE ASSEMBLY,
- the spellings `__attribute`, `__signed`, and `__volatile`.

These GNU builtins are recognized, in constant expressions too where the result is constant:

```
__builtin_expect(e, c)            the value of e, as long
__builtin_constant_p(e)           1 if e is a constant expression;
                                  e is not evaluated
__builtin_types_compatible_p(t1, t2)
                                  1 if the types are compatible
__builtin_choose_expr(c, e1, e2)  e1 if the constant c is nonzero,
                                  otherwise e2; only one is compiled
__builtin_offsetof(type, member)  the offset of a member designator
                                  such as a.b[3].c; offsetof uses it
__builtin_unreachable()           compile to ud2
__builtin_trap()                  compile to ud2
```

The builtin `__syscall(nr, ...)` makes Linux system call `nr` with up to six arguments and yields the raw result.

The preprocessor handles object-like and function-like macros, `#`, `##`, `__VA_ARGS__`, the GNU form `, ## __VA_ARGS__`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, `defined`, `#undef`, `#error`, `#warning`, `#line`, and `#pragma once`. Other pragmas are ignored. Macro arguments are expanded before substitution. The predefined macros are `__STDC__`, `__STDC_VERSION__` (199901L), `__STDC_HOSTED__` (1, or 0 with *-ffreestanding*), `__x86_64__`, `__x86_64`, `__amd64__`, `__linux__`, `__linux`, `__unix__`, `__unix`, `__LP64__`, `_LP64`, `__ELF__`, `__CHAR_BIT__`, `__SIZEOF_INT__`, `__SIZEOF_LONG__`, `__SIZEOF_LONG_LONG__`, `__SIZEOF_SHORT__`, `__SIZEOF_POINTER__`, `__FILE__`, and `__LINE__`. `__extension__` expands to nothing.

These attributes take effect. An attribute name may also be spelled with underscores around it, as `__aligned__`.

```
packed           on a structure or union: each member gets an
                 alignment of 1, and bit fields are packed bit by
                 bit, as gcc packs them
aligned(n)       raise the alignment of a variable, member, function,
                 structure, or union to n, a power of two; on a
                 typedef, set it; without n, 16
section("name")  place a variable or function in a section of its
                 own, as described under SECTIONS
weak             make a symbol weak, as described under SECTIONS
alias("target")  make the declared name another name for target,
                 which is defined in the same file
interrupt        make a function an interrupt handler, as described
                 under INTERRUPT HANDLERS
naked            leave out the prologue and epilogue of a function;
                 its body may contain only asm statements, which
                 must return themselves
```

`packed` and `aligned` go after `struct` or `union`, or after the closing brace. On a declaration an attribute may come before the type or after the declarator. Other attributes, such as `noreturn`, `used`, `unused`, and `format`, are accepted and ignored. `_Alignas(n)` and `_Alignas(type)` work as `aligned` on the declared object.

## ASSEMBLY

The assembler reads AT&T syntax as GNU as does: the source operand comes first, registers are written `%rax`, immediates `$5`, and memory operands `disp(base, index, scale)`. A *.S* file is preprocessed like C, with `__ASSEMBLER__` defined as 1, so a header can serve C and assembly when its C parts are inside `#ifndef __ASSEMBLER__`. In a *.s* file, and after the first token of a line in a *.S* file, `#` starts a comment. `/* */` and `//` comments work in both. Statements end at a newline or at `;`, and a line may start with any number of labels.

Operands:

```
%reg               a register: the general registers in all sizes,
                   %ah to %bh, %r8b to %r15b, %spl to %dil, segment
                   registers, %cr0 to %cr15, %dr0 to %dr15, %xmm0 to
                   %xmm15
$expr              an immediate
expr               an absolute address
disp(base, index, scale)
                   any part may be left out; the registers may be
                   64-bit, 32-bit (with an address size prefix in
                   64-bit code), or the 16-bit pairs (%bx,%si) and
                   the like
disp(%rip)         relative to the next instruction, in 64-bit code
%seg:mem           a segment override
*%reg  *mem        the target of an indirect jmp or call
```

`.code16`, `.code32`, and `.code64` select the size of the code that follows, so a real-mode or protected-mode trampoline can be written in source. Operand and address size prefixes are added where an operand needs them.

A label is `name:`. A numeric label such as `1:` may be defined many times: `1b` refers to the nearest one before and `1f` to the nearest one after. Symbols that begin with `.L` and numeric labels stay out of the symbol table. `.` is the current address. `name = expr`, `.set`, and `.equ` give a name to a constant or to an address.

Expressions use the C operators with C precedence. A sum or difference may contain symbols: the difference of two labels in the same section is a constant, and `label - other` where *other* lies in the section being written becomes a PC-relative value, as in jump tables in `.rodata`. A symbol that is defined further down may be used anywhere a relocation can hold the value, even inside `*`, `/`, and the other operators, which are evaluated once the file has been read. Where the size of an instruction depends on a value, as in `add $K, %eax` or `K(%rbp)`, a value known at that point selects the shortest form and an unknown one the longest, as GNU as does. Jumps and conditional jumps to a label in the same section use an 8-bit displacement when it reaches, and a longer one otherwise. Calls always use the long form.

Directives:

```
.text .data .bss .rodata .section name[, "flags"[, @type]]
.pushsection .popsection .previous
.code16 .code32 .code64
.globl .global .local .weak .hidden .type .size
.set .equ .comm .lcomm
.byte .word .short .hword .value .long .int .quad
.ascii .asciz .string .zero .skip .space .fill .incbin
.align .balign .p2align .org
.if .ifdef .ifndef .ifeq .ifne .ifgt .ifge .iflt .ifle .elseif .else .endif
.file .ident .loc .cfi_* .att_syntax .end
```

`.if expr` assembles the lines up to the matching `.elseif`, `.else`, or `.endif` when *expr*, which must be known at that point, is not zero, and `.ifdef sym` when *sym* is defined above. `.align` takes a number of bytes, as on x86 ELF. Code is padded with `nop` (0x90), and other sections with zeros. `.section` places code in a section with the flag `x`, read-only data with `a`, data with `w`, and `.bss` and sections of type `@nobits` in `.bss`. Sections are laid out as described under SECTIONS.

The instructions are

```
arithmetic   add or adc sbb and sub xor cmp test inc dec neg not
             mul imul div idiv lea
shifts       rol ror rcl rcr shl sal shr sar shld shrd
moves        mov movabs movzb* movzw* movsb* movsw* movslq movzx
             movsx movsxd xchg xadd cmpxchg cmpxchg8b cmpxchg16b
             cmovCC setCC bswap cbtw cwtl cltq cwtd cltd cqto
bits         bt bts btr btc bsf bsr popcnt lzcnt tzcnt
stack        push pop pushf popf pusha popa leave
control      jmp jCC call ret lret iret loop loope loopne jcxz
             jecxz jrcxz ljmp lcall int int3 syscall
             sysret sysenter sysexit
strings      movs cmps stos lods scas ins outs, with rep repe
             repz repne repnz
system       cli sti hlt pause nop clc stc cld std cmc lahf sahf
             in out lgdt lidt sgdt sidt lldt sldt ltr str lmsw
             smsw verr verw invlpg invd wbinvd clts cpuid rdtsc
             rdtscp rdmsr wrmsr rdpmc swapgs mfence lfence sfence
             clflush monitor mwait xgetbv xsetbv fxsave fxrstor
             xsave xrstor ldmxcsr stmxcsr ud2, and mov to and
             from control, debug, and segment registers
SSE          movd movq movaps movups movapd movupd movdqa movdqu
             movss movsd, the add sub mul div min max sqrt forms of
             ss sd ps pd, and andps andpd andnps andnpd orps orpd
             xorps xorpd pxor por pand paddq ucomiss ucomisd comiss
             comisd cvtsi2ss cvtsi2sd cvtss2si cvtsd2si cvttss2si
             cvttsd2si cvtss2sd cvtsd2ss
prefixes     lock rep repe repz repne repnz data16 data32 addr16
             addr32
```

A suffix `b`, `w`, `l`, or `q` sets the operand size, which otherwise comes from the register operands. An instruction with neither, such as `mov $1, (%rax)`, is an error. The encodings are those GNU as chooses, which *tools/ascorpus.py* checks.

Symbols are shared with the C files of the same run. A label made global with `.globl` can be called or read from C, and assembly can call C functions and use C variables by name. A label in a code section is a function and other labels are objects, unless `.type` says otherwise. `.weak` makes a definition give way to an ordinary one, and an undefined weak symbol has the address 0, as for C. The value of an undefined symbol is filled in when the program is linked.

## INLINE ASSEMBLY

`asm`, `__asm__`, and `__asm` take a template string that the built-in assembler, described under ASSEMBLY, reads in place. `volatile` and `inline` may follow the keyword and are accepted; every asm statement is kept where it is written. Basic asm, `asm("...")`, is copied as it is, inside a function or at file scope, where it may define functions and data. Extended asm adds operands:

```
asm [volatile] ( template : outputs : inputs : clobbers );
asm [volatile] goto ( template : : inputs : clobbers : labels );
```

An operand is `[name] "constraint" (expression)`, and the name is optional. Outputs are lvalues. In the template, `%0`, `%1` and so on name the operands in order, outputs first, then inputs, then labels, and `%[name]` names one by its name. `%%` is a single `%`, and `%=` is a number that differs for each asm statement, for unique labels. A letter between `%` and the operand changes how it is written:

```
%b0 %w0 %k0 %q0  the 8, 16, 32, or 64-bit name of a register
%h0              %ah, %bh, %ch, or %dh
%c0              a constant or symbol without the $
%n0              the negated constant without the $
%a0              the register as an address, (%rax)
%z0              the suffix b, w, l, or q for the operand size
%l0              a label of asm goto
```

Without a letter a register is written in the size of its operand's type, a memory operand as `-8(%rbp)`, `symbol(%rip)`, or `(%reg)`, and a constant as `$123` or `$symbol`.

Constraints:

```
r q R Q g        any general register
a b c d S D      %rax, %rbx, %rcx, %rdx, %rsi, %rdi
m o V            memory
i n s I J K L M N O e Z
                 a constant; i and s also take the address of a
                 global or a function; N only 0 to 255, so "Nd"
                 is a constant port or %dx
0 to 9, [name]   the same place as that output
=  +             output, and output that is also read
&                early clobber: the output gets a register of its own
```

When a constraint allows several kinds, a constant expression becomes an immediate if allowed, otherwise a register is chosen if allowed, otherwise memory. Clobbers may name registers in any size, with or without `%`, and `memory`, `cc`, and the SSE and x87 registers, which need nothing. A clobbered or used `%rbx` or `%r12` to `%r15` is saved around the statement. `%rsp` and `%rbp` cannot be clobbered.

The registers of the operands are loaded just before the template and the outputs stored just after it. Every operand gets a register of its own unless it matches an output, so a statement needs at most 14 general registers. `asm goto` may jump to the C labels of the function; it takes no outputs and cannot use `%rbx` or `%r12` to `%r15`. Labels may be anywhere in the function.

A declaration may end with `asm("name")`. On a global or external declaration the symbol gets that name. On a local variable it names a register, as in

```
register long r10 asm("r10") = flags;
asm volatile("syscall" : "=a"(ret) : "a"(n), "r"(r10) : "rcx", "r11");
```

and an operand that is that variable alone uses the register.

## LIBRARY

Unless *-nostdlib* is given, the built-in library is compiled into each executable. A function defined by the program replaces the library function of the same name.

The headers are *assert.h*, *ctype.h*, *errno.h*, *fcntl.h*, *float.h*, *inttypes.h*, *limits.h*, *math.h*, *stdalign.h*, *stdarg.h*, *stdbool.h*, *stddef.h*, *stdint.h*, *stdio.h*, *stdlib.h*, *stdnoreturn.h*, *string.h*, *unistd.h*, *sys/mman.h*, *sys/stat.h*, and *sys/types.h*. They declare these functions:

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

These builtins compile to single instructions or short sequences, for kernels and drivers. Arguments are integers or pointers. *baremetal/kernel.h* shows how to write each of them with inline assembly instead.

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

## SECTIONS

`__attribute__((section("name")))` places a variable or a function in a section named *name*. Sections are kept apart by kind and laid out after the built-in section of the same kind:

```
code       a section that holds functions; after .text
read-only  a section whose name begins with .rodata; after .rodata
data       any other section that holds variables; after .data
```

A variable in a section is stored in the file even when it has no initializer, and its own alignment is used, so consecutive entries of one type form an array. The names *.text* and *.data* mean the built-in sections, and *.bss* does too for a variable without an initializer. One section cannot hold both functions and variables. All files on the command line share their sections, and a section keeps the order of its definitions.

As in GNU ld, the symbols `__start_name` and `__stop_name` are defined as the addresses of the first byte of section *name* and the byte after its end, when *name* is a valid C identifier and the program declares them. A table built from separate files can be walked with them:

```
struct cmd { const char *name; void (*run)(void); };
#define CMD(n) static const struct cmd cmd_##n \
	__attribute__((section("cmds"), used)) = { #n, n }
extern const struct cmd __start_cmds[], __stop_cmds[];

for (const struct cmd *c = __start_cmds; c < __stop_cmds; c++)
	c->run();
```

A weak definition gives way to an ordinary definition of the same name in another file. If every definition is weak, the first one is used. A weak symbol that is declared and never defined has the address 0, so `if (hook) hook();` calls *hook* only when some file defines it. Weak symbols are marked `STB_WEAK` in the symbol table. A function in the built-in library gives way to a weak definition in the program as it does to any other.

## OUTPUT FORMATS

**elf** is the default. A hosted program is the executable described under IMPLEMENTATION. With *-ffreestanding* the file has no startup code, and its entry address is the entry point. *-Ttext* moves the whole program.

**bin** is a flat binary: `.text`, `.rodata`, and `.data`, each followed by the sections of its kind described under SECTIONS, back to back. It is loaded at the *-Ttext* address, 0 by default, and execution starts at its first byte, so the entry point is the first function in the first file. `.bss` follows `.data` in memory.

With a linker script, *bin*, *img*, and *multiboot* hold the flat binary described under LINKER SCRIPTS, and the script gives the addresses that *-Ttext* gives otherwise.

**img** is a raw disk image for a PC BIOS. Sector 0 is a boot sector written by *sccc*, and the kernel, as a flat binary, starts at sector 1. The boot sector

1. reads the BIOS memory map (E820) to 0x5000: a 32-bit count, then entries of 24 bytes (base, length, type, extended attributes) from 0x5008,
2. loads the kernel to 0x10000 with `int 13h`,
3. enables the A20 line,
4. identity maps the first 1 GB with 2 MB pages, using page tables at 0x1000 to 0x3fff, and maps it at 0xffffffff80000000 too when the kernel is linked there,
5. switches to 64-bit long mode,
6. copies the kernel to its *-Ttext* address, 0x100000 by default, and clears its `.bss`,
7. calls the entry point with `rdi` = 0x5000 and `rsp` = 0x80000, with interrupts disabled.

The kernel loads between 1 MB and 1 GB and is at most 588800 bytes. The image is padded to whole cylinders of 16 heads and 63 sectors, 516096 bytes each.

**multiboot** is a Multiboot 1 kernel for GRUB and for `qemu -kernel`. Its first page holds the Multiboot header, with load addresses, and a trampoline from 32-bit protected mode to long mode. The kernel follows at the *-Ttext* address, 0x101000 by default, which must be page aligned. The header holds physical addresses. The trampoline identity maps the first 1 GB, also at 0xffffffff80000000 for a higher-half kernel, and calls the entry point with `rdi` = the address of the Multiboot information structure, `rsi` = 0x2BADB002, and a 16 KB stack. The page tables and the stack lie after `.bss`.

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

The C code generator and the assembler write instructions through the same encoder. It builds REX prefixes, ModRM and SIB bytes, and displacements. An assembly file is read in passes: each pass assembles the whole file, and a branch that turned out too far for an 8-bit displacement is made long in the next pass, until no branch changes. Values that were not known when an instruction was written are filled in at the end, or left to the linker. The boot sector of an *img* and the first page of a *multiboot* image are assembly source in *lib/boot/*, assembled when the image is written with the addresses set as constants.

A hosted program is a position dependent executable (`ET_EXEC`) loaded at 0x400000, with two loadable segments:

| Segment | Flags | Sections                                                        |
|---------|-------|-----------------------------------------------------------------|
| 1       | R X   | ELF and program headers, .text, code sections, .rodata, read-only sections |
| 2       | R W   | .data, data sections, .bss                                      |

The file also has `.symtab`, `.strtab`, and `.shstrtab`, so *readelf*, *objdump*, and *gdb* can read it. *sccc* resolves all relocations itself, and the file has none. The entry point `_start` sets up `argc`, `argv`, and `environ`, initializes standard I/O, calls `main`, and passes its value to `exit`. A hello world program is about 84 KB, or about 5 KB with *-nostdlib*.

Calls follow the System V x86-64 convention. Each argument is classified by its eightbytes: integers and pointers are INTEGER, `float` and `double` SSE, and a structure or union of at most 16 bytes takes the class of its members in each eightbyte, INTEGER winning over SSE. INTEGER eightbytes go in `rdi`, `rsi`, `rdx`, `rcx`, `r8`, and `r9`, SSE ones in `xmm0` to `xmm7`. A larger or unaligned structure, and any argument that no longer fits in the registers left, is copied to the stack. Results return in `rax` and `rdx`, or `xmm0` and `xmm1`; a structure returned in memory gets a hidden pointer in `rdi`, which comes back in `rax`. `al` holds the number of vector registers used. A variadic function saves all argument registers, `va_list` has the System V layout, and `va_arg` is the builtin `__builtin_va_arg`, so objects need no helper from the library. Values keep their own representation inside a function: floating values sit in `rax` as raw bits and move to and from the `xmm` registers at calls and returns.

The headers and library live in *lib/*. *tools/embed.sh* turns them into C string tables in *builtin.h*, which *sccc.c* includes. The library is split into strings of 40 lines, each shorter than the 4095 bytes ISO C allows. *tools/embed.sh* also embeds *lib/boot/*. After a change in *lib/*, run

```
make embed
make
```

## TESTING

```
make check
```

runs `make bootstrap`, each suite below with both *sccc* and *sccc2*, and the bare metal tests with *sccc*.

*test_sccc.c* is a harness of 169 cases. It writes each program to */tmp/tc_src.c*, and the assembly cases also to */tmp/tc_asm.s* or */tmp/tc_asm.S*, compiles them, runs the program, and checks the exit status. The object cases compile two files with *-c* and link them, and the script cases link a program through */tmp/tc_script.ld*. Eighteen of the C cases must fail to compile, nine of the assembly cases must fail to assemble, two of the object cases must fail to link, and seven of the script cases must fail. Run it by hand with

```
make test
./test_sccc ./sccc
```

*tests/run.sh* compiles and runs each program in *tests/*, together with its *NAME_partN.c*, *.S*, or *.s* files. A test passes if it exits 0 and, when *NAME.out* exists, its standard output and standard error match that file. Tests without an output file check their own results and report through the exit status. There are 21 tests. *t17_gnu* covers the C11 and GNU language extensions, *t18_attr* the attributes and sections, across two files, and *t19_asm* calls between C and an assembly file: a jump table in `.rodata`, local labels, `.comm`, weak symbols, and a call to `printf` from assembly. *t20_inline* covers inline assembly: every constraint and modifier, `asm goto` forward and back, `naked`, file scope asm, asm labels, register variables and a raw system call, and *t21_atomic* the atomic operations a kernel writes with `lock`. The *.out* files were produced by gcc. *tests/gcccheck.sh* builds the same tests with gcc to check the tests themselves. `tests/run.sh compiler objects` compiles each file with *-c* and links the objects instead.

*tests/link/run.sh* builds one program from ten files and an archive in eight ways: every file by gcc or by *sccc*, or a mixture, linked by *sccc* or by gcc. The program walks a section table that gcc and *sccc* objects fill, uses common symbols, a weak function overridden in another file, an archive whose unused member refers to a missing symbol, and a *-fPIC* object, and its output must match *link.out*, from gcc.

*tests/ldscript/run.sh* links the same objects with GNU ld and with *sccc* through each of its scripts and compares the results: a flat binary byte for byte, an ELF file by its allocated sections, symbols, and section contents. The objects of a kernel-like program come from *sccc*, from gcc and as, and from both, with one of them in an archive, and the scripts use most of the commands: a flat image with section tables and data commands, a higher-half layout with `AT` and `NOLOAD`, `PHDRS`, and orphans. Twenty more scripts place a few sections of *orphans.s* and leave the rest to the orphan rules. Last, a Linux program is linked through *hosted.ld* in one step, from an object of *sccc*, and from one of gcc, and must print *hosted.out*. `make ldscript` runs it.

```
make ldfuzz
```

runs *tools/ldfuzz.py* for 200 rounds. Each round generates up to three assembly files with random sections, alignments, symbols, and relocations, and a random script with input patterns, sorting, alignment, addresses, `AT`, assignments, expressions, data commands, and fills, half of them for binary output. The files are assembled by GNU as or by *sccc*, linked by GNU ld and by *sccc*, and compared. A failing case is saved in *ldfuzz_fail_SEED/*. `make check` runs 20 rounds.

```
make abifuzz
```

runs *tools/abifuzz.py* for 50 rounds. Each round generates random structures of integers, floating values, arrays, and nested structures, and functions that take up to 12 of them and return one, some variadic. The callers go in one file and the functions in another, and each pair is built five ways, mixing gcc and *sccc* objects and both linkers. The output must match a build made entirely by gcc. A failing pair is saved as *abi_fail_SEED_caller.c* and *abi_fail_SEED_callee.c*. `make check` runs 10 rounds, and `make link` runs *tests/link/run.sh*.

```
make ascheck
```

runs *tools/ascorpus.py*, which compares the assembler with GNU as. It generates about 13500 instructions for 64-bit, 32-bit, and 16-bit code, every instruction group with each register, addressing mode, and immediate size, and gives each instruction a slot of 16 bytes so that each is compared on its own. Lines GNU as rejects are left out. It then compares whole files: 20 random files of forward and backward branches across gaps of 1 to 200 bytes, a file of data directives and expressions, and a boot sector that switches from 16-bit to 32-bit to 64-bit code. GNU as and *ld* serve only as the oracle. The environment variable `SEED0` sets the seed. `make check` runs it with *sccc* and *sccc2*, and it is skipped when binutils are not installed.

*examples/run.sh* checks the example programs, as described below.

```
make baremetal
```

or `baremetal/run.sh [compiler]` builds each kernel in *baremetal/*, with its *NAME_partN* files, as a disk image and boots it in QEMU and in Bochs, without a window. Each kernel is built three times: with the machine builtins, with `-DASM_BUILTINS`, where *kernel.h* replaces them with inline assembly, and from objects, each file compiled with *-c* and the objects linked into the image. A kernel passes when its serial output matches *NAME.out*, and in QEMU when it also exits through the debug exit device. The kernels are

```
hello       serial port and VGA text
exceptions  IDT, #BP, #DE, and #PF with an error code
timer       PIT interrupts through the PIC
keyboard    PS/2 keyboard interrupts and echo
memory      page frame allocator and heap over the E820 map
initcall    init functions run from a section table, its own
            page tables with a higher half alias, threaded
            dispatch, weak and alias symbols
tasks       interrupt stubs and a context switch in a .S file:
            #BP, #DE, #UD, and #GP with its error code, timer
            interrupts, three tasks switched by int $0x80 through
            saved interrupt frames, a coroutine switched by
            switch_to, assembly calling C through a table
```

Each kernel is also linked through *baremetal/kernel.ld*, a script with the layout that *img* gives without one, from source and from objects, and the image must be the same byte for byte. *baremetal/higher/* is a kernel linked at 0xffffffff80000000 and loaded at 1 MB. It is built with its own Multiboot header and boot code in *boot.S* through *kernel.ld* and booted with `qemu -kernel`, from source and from objects, where GNU ld must link the objects into the same image; and through *img.ld* with the boot code of *img*, in QEMU and Bochs, and of *multiboot*. It checks that it runs at its high addresses, that its data is at the load address below, its `.bss`, a sorted table of init functions, the symbols of the script, and an interrupt handler at a high address.

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

or `examples/run.sh [compiler]` compiles each program with *compiler* and runs it in a temporary directory. The default compiler is the *sccc* in the directory above *examples/*. *NAME.in*, if present, is the standard input; otherwise it is */dev/null*. The words of *NAME.args*, if present, are the arguments. Files named *NAME_partN.c* are compiled with *NAME.c*. The program must exit 0, and its standard output must match *NAME.out*, which was produced by gcc. When *timeout* exists each program is limited to 10 seconds. `examples/run.sh compiler -update` rebuilds the *.out* files with gcc. `examples/run.sh compiler objects` compiles each file with *-c* and links the objects. The script exits nonzero if any program fails.

## FILES

```
sccc.c              the compiler
builtin.h           generated by make embed; do not edit
lib/include/        sources of the built-in headers
lib/libc.c          source of the built-in library
lib/boot/           boot sector and multiboot trampoline
tools/embed.sh      writes builtin.h from lib/
tools/fuzz_expr.py  random expression tester
tools/ascorpus.py   compares the assembler with GNU as
tools/abifuzz.py    compares calls between gcc and sccc objects
tools/ldfuzz.py     compares linker scripts with GNU ld
test_sccc.c         exit status test harness
tests/              language tests and expected output
tests/link/         linking with gcc objects and archives
tests/ldscript/     linker scripts compared with GNU ld
examples/           example programs, inputs, and expected output
baremetal/          test kernels, expected serial output, run.sh,
                    kernel.ld, and the higher-half kernel in higher/
Makefile            build, bootstrap, and test targets
COPYING             the GNU General Public License, version 3
```

## SEE ALSO

gcc(1), elf(5), syscall(2), qemu-system-x86_64(1), bochs(1)

## BUGS

Variable length arrays are not supported.

K&R function definitions are not supported.

Wide characters and wide strings are not supported.

`_Complex` is not supported.

A local variable may be aligned to at most 16 bytes, the alignment of the stack frame.

In `[a ... b].m = e`, a range designator followed by a member designator, `e` is evaluated once for each element.

Universal character names are not recognized. `'\u0041'` is accepted and is not `'A'`.

Trigraphs and digraphs are not recognized.

The assembler has no `.macro`, `.rept`, or `.irp`; use the preprocessor in a *.S* file. `.if` needs a value known where it stands. It has no x87, AVX, or Intel syntax. `.set` needs a value or an address that is already defined. Expressions follow C precedence, which differs from GNU as for a few mixtures such as `a | b + c`. Code alignment uses one-byte `nop`s where GNU as uses longer ones. `jcxz`, `loop`, and the other branches with only an 8-bit form fail when the target is out of reach.

Inline assembly has no constraints for SSE or x87 registers and no `A`; a floating operand goes in a general register as raw bits. `asm goto` takes no outputs. Each register operand gets its own register, so a statement may run out of registers where gcc would share them. Numeric labels such as `1:` are local to one asm statement. An asm statement inside `sizeof` or `typeof` still leaves anything it put in other sections.

`long double` is `double`.

The math functions are not correctly rounded. `sqrt` can be one unit in the last place off. `sin`, `cos`, and `log` have absolute errors of about 1e-15, so their relative error is large near their zeros.

`printf` has no `%a`.

There is no `scanf` family, and no `time.h`, `signal.h`, or `setjmp.h`.

`setvbuf` ignores its buffer argument.

The *-I* directories are also searched when the built-in library is compiled. A file there named like a built-in header replaces it for the library too, and the build fails.

Input files after the 64th are ignored without a message. A 65th *-I* or *-D* is reported as an unknown argument.

`long double` is passed as `double`, unlike gcc, which uses the x87 format in memory.

The linker does not run `.init_array`, `.fini_array`, or constructors, and does not support thread-local storage, shared libraries, or COMDAT groups. Symbols referenced only by files after an archive do not pull members from it, as with GNU ld.

Linker scripts have no `MEMORY` regions, `OVERLAY`, `INCLUDE`, `INPUT`, `GROUP`, `INSERT`, `SUBALIGN`, `ONLY_IF_RO` or `ONLY_IF_RW`, and `KEEP` changes nothing because no sections are collected. A `FILL` applies to the whole of each later gap; the `FILEHDR` and `PHDRS` keywords of `PHDRS` are accepted but the headers are not loaded. GNU ld places an empty output section that has an address, and the orphans around its own internal sections (`.got`, `.iplt`), in ways that depend on the statements around them; *sccc* skips an empty section, and keeps the difference between address and load address across sections GNU ld would restart. Without a section of their kind in the script, read-only orphans next to a section that mixes code and data may land elsewhere than with GNU ld. The output has no `PT_GNU_STACK` and no headers in a loaded segment.

A bit field in a packed structure may span at most 8 bytes.

The only target is x86-64.

## LICENSE

Copyright (C) 2026 Ivan Gaydardzhiev. *sccc* is distributed under the GNU General Public License, version 3 only. See *COPYING*.
