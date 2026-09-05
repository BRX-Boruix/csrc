"""csrc/build_c.py - compile a freestanding C program into a single ELF64 for BORUIX.

Reusable C-runtime build (T2-0 substrate, used by T2-3/4/5 too). Builds:
  crt0.o   - entry _start -> main (csrc/crt0.S)
  crtrt.o  - runtime: syscall glue + exit/puts (csrc/crtrt.c), + any extra rt sources
then links: crt0.o + crtrt.o + <prog>.o [+ extra rt .o] -> <prog>.elf (ET_EXEC, EM_X86_64).

Usage:
  python build_c.py <prog_name> <prog_src_dir> <out_dir> [extra_rt_c1 extra_rt_c2 ...]
  - prog_name: e.g. chelldemo
  - prog_src_dir: dir containing <prog_name>.c (single translation unit)
  - out_dir: where <prog_name>.elf is written
  - extra_rt_cN: additional runtime .c under csrc to compile+link (e.g. malloc.c)
Calls the LLVM clang/lld toolchain. Zero external deps beyond the local clang install.
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLANG_BIN = os.environ.get("CLANG_BIN", r"F:\clang\18.1.8x86_64\bin")
CLANG = os.path.join(CLANG_BIN, "clang.exe")
LLD = os.path.join(CLANG_BIN, "ld.lld.exe")
TARGET = "x86_64-unknown-none"

def run(cmd):
    r = subprocess.run(cmd)
    if r.returncode != 0:
        raise SystemExit(f"command failed ({r.returncode}): {' '.join(cmd)}")

def main():
    if len(sys.argv) < 4:
        print(__doc__); return 1
    prog, src_dir, out_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    extra_rt = sys.argv[4:]
    os.makedirs(out_dir, exist_ok=True)
    cc = [CLANG, "--target=" + TARGET, "-ffreestanding", "-fno-builtin",
          "-fno-stack-protector", "-fno-pic", "-O2", "-fno-stack-protector"]
    # crt0.S -> crt0.o
    run(cc + ["-c", os.path.join(HERE, "crt0.S"), "-o", os.path.join(out_dir, "crt0.o")])
    # crtrt.c + extra rt -> objects
    rt_srcs = [os.path.join(HERE, "crtrt.c")] + [os.path.join(HERE, x) for x in extra_rt]
    rt_objs = []
    for i, s in enumerate(rt_srcs):
        o = os.path.join(out_dir, f"rt{i}.o")
        run(cc + ["-c", s, "-o", o]); rt_objs.append(o)
    # program .c -> object
    prog_c = os.path.join(src_dir, prog + ".c")
    prog_o = os.path.join(out_dir, prog + ".o")
    run(cc + ["-c", prog_c, "-o", prog_o])
    # link
    out = os.path.join(out_dir, prog + ".elf")
    objs = [os.path.join(out_dir, "crt0.o")] + rt_objs + [prog_o]
    run([LLD, "-o", out, "-e", "_start", "-nostdlib", "--no-dynamic-linker"] + objs +
        ["-z", "noexecstack", "-T", os.path.join(HERE, "linker.ld")])
    print(f"[crt] {prog}.elf -> {out}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
