# csrc

BORUIX's freestanding C runtime: the minimum needed to compile and run ordinary C programs on this system.

[简体中文](README.md)

No external C library: no full standard library, no file streams — just program startup, system
calls, threading and synchronisation, and minimal formatted output. Everything talks to the kernel
through system calls directly.

## Contents

- `crt0.S` — the entry point `_start`, which sets up and calls `main`
- `crtrt.c` — syscall glue, `exit`, string output
- `thread.c` — memory mapping, thread create/join/exit, kernel sync words
- `pthread.c`, `pthread.h` — the thread lifecycle interface
- `pthread_sync.c` — mutexes, condition variables, semaphores
- `stdio.c` — minimal formatted output (`%s %c %d %u %x`)
- `boruix.h` — public declarations
- `linker.ld` — user-space section layout
- `build_c.py` — compiles and links a single C program into an executable
- `prog/` — example programs

## Syscall convention

System calls go through `int 0x80` with the register assignment the kernel expects. Errors are
returned as negative errno values and callers test for less than zero — deliberately unlike the C
standard library's global `errno`.

## Threads and synchronisation

What is provided is a POSIX-flavoured subset, not a complete pthread implementation:

- Thread return values are integer exit codes only, passed through the runtime's thread descriptor, not the kernel
- `pthread_join` may only be called from the group leader — a limit of the kernel's reaping mechanism
- `pthread_attr_t` is ignored; custom thread attributes are not supported
- Synchronisation objects are complete types, declarable as globals or stack variables; blocking and waking are done by the kernel

## Example programs

The programs under `prog/` are both verification and usage examples:

- `chelldemo.c` — the smallest C program: entry, arguments, output, exit
- `pthreaddemo.c` — creating several threads, each returning a unique value, joined and checked
- `pthread_syncdemo.c` — mutexes, condition variables, semaphores
- `pthread_bench.c` — a recursive create-and-join benchmark
- `forkdemo.c` — `fork()` and `waitpid()`

## Building

```bash
python build_c.py <prog_name> <prog_src_dir> <out_dir> [extra_runtime_c_files...]
python build_c.py chelldemo prog _build
```

Requires clang (cross target `x86_64-unknown-none`) and an LLD linker. The script probes for a
usable LLD at run time and prints the one it settled on, so non-default locations work too.

## Repository layout

```
csrc/
├── crt0.S, crtrt.c      # entry and runtime
├── thread.c, pthread.c, pthread_sync.c
├── stdio.c, boruix.h, linker.ld
├── build_c.py           # the build script
└── prog/                # example programs
```

## Related projects

- [`libsys`](https://github.com/BRX-Boruix/libsys) — the Rust-side syscall wrappers, independent of this repository
- [`selftest`](https://github.com/BRX-Boruix/selftest) — runs the examples as regressions

## License

MIT License, copyright Yang Borui. See [LICENSE](LICENSE).
