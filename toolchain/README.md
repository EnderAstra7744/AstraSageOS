# AstraSageOS toolchain (cross compilation)

Compiles C and C++ into programs that the AstraSageOS loader (`kernel/native.c`) runs. It runs on the **build computer**
(Linux with gcc, clang, binutils, python3), never inside the kernel; the OS contains no compiler.

```
toolchain/as-cc [--backend gcc|clang|tcc] [--libc] [-Os] -o hello.elf hello.c      # build only; nothing is executed
python3 toolchain/as-check.py hello.elf                                           # does the loader accept it?
toolchain/build-tcc.sh                                                            # optional tcc backend (needs GitHub access)
python3 toolchain/tests/run_tests.py                                              # stage tests (make test runs it)
```

## What the target is (read from the sources, not assumed)
| Fact | Where it comes from |
|---|---|
| 32-bit x86 (i386), cdecl, no SSE/MMX | `Makefile` (`-m32 -mno-sse -mno-mmx`), `boot/` |
| One ELF32 `ET_EXEC` file, segments copied to their `vaddr`, `vaddr >= 0x05000000`, no dynamic linking | `kernel/native.c` |
| Entry `int as_main(const as_api *, const char *)`; with `--libc` write `int main(int, char**)` instead | `native/as_api.h`, `native/libc/libc.c` |
| No system calls: everything goes through the `as_api` table (window, files, HTTP, `exec`, `aspi`) | `native/as_api.h` |
| Runs in kernel mode, no memory protection | `kernel/native.c` |
| Global constructors: `.init_array` is kept by the linker script | `native/app.ld` |

## Stages (each is checked on its own)
1. compile: backend → object (syntax and semantic errors come with `file:line` straight from the compiler)
2. link: `ld -T native/app.ld` (+ the C library object with `--libc`); undefined symbols are reported
3. check: `as-check.py` verifies ELF32/i386/EXEC, base address, no dynamic section, entry inside the image
4. install: the output is written next to the target and renamed only after 1–3 succeeded — a failed build never damages an earlier output

Building and running are separate: `as-cc` never runs the program. Running is tested by `sys/tests/gui_test.py`, which loads the
built programs (gcc, clang, tcc) through the real `native_run()` from `kernel/native.c` in the GUI harness (a 32-bit process, not real hardware).

## Backends
- **gcc**, **clang** (`--target=i386-pc-none-elf`): C and C++ (`-fno-exceptions -fno-rtti`, no libstdc++).
- **tcc** (TinyCC, built by `build-tcc.sh` as an i386 cross compiler): C only, its own code generator and assembler; the same `ld` script links the result.
- New compilers plug in as one `case` in `compile()` of `as-cc`: they only have to produce an i386 ELF relocatable object.

## What does NOT exist (honest list)
- **No compiler runs inside AstraSageOS.** The kernel limits every file to 4096 bytes (`FS_FILE_MAX`) and the file system to 256 nodes; a real compiler inside the OS
  could neither store normal source files nor its own output, and the native libc has no `fopen` on the OS files beyond the 4096-byte API. Raising these limits (a real block file system) is the prerequisite for porting tcc to run natively.
- Therefore the **ASPI integration** (point 9 of the spec) is not done: an ASPI package can only install a compiler that runs on the OS. The cross toolchain stays outside.
- No debugger, no shared libraries, no libc++/exceptions/RTTI, no position-independent output.
- Output programs of up to 4096 bytes can be run from a file inside the OS (`nrun FILE`); larger ones can only be embedded in an image or ASPI package (as-check says so).
