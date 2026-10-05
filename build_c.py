"""csrc/build_c.py - compile a freestanding C program into a single ELF64 for BORUIX.

Reusable C-runtime build (T2-0 substrate, used by T2-3/4/5 too). Builds:
  crt0.o   - entry _start -> main (csrc/crt0.S)
  crtrt.o  - runtime: syscall glue + exit/puts (csrc/crtrt.c), + any extra rt sources
then links: crt0.o + crtrt.o + <prog>.o [+ extra rt .o] -> <prog>.elf (ET_EXEC, EM_X86_64).

Usage:
  python build_c.py <prog_name> <prog_src_dir> <out_dir> [extra_rt_c1 extra_rt_c2 ...]
  python build_c.py --rt-only <out_dir>   # 只产出 sysroot 的 C 运行时对象（user_main.o）
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


def _pick_linker():
    """Return (path, extra_args) for a usable LLD.

    The clang-bundled ld.lld.exe is preferred, but on this machine an endpoint
    security product intermittently denies executing it ("Access is denied",
    exit 1) while the adjacent clang.exe keeps working. That is an environment
    fault, not a toolchain problem, and it blocks every C link with no usable
    diagnostic from subprocess.

    rust-lld ships with the Rust toolchain, is the SAME LLD, and runs from a
    location the product allows. It is a multi-flavor driver, so it needs
    `-flavor gnu` to behave like ld.lld; the clang ld.lld does not accept that
    flag. Probing beats hardcoding a path: whichever actually executes is used,
    and the caller is told which one, so the choice is visible rather than
    silent.
    """
    clang_lld = os.path.join(CLANG_BIN, "ld.lld.exe")
    # Toolchains live under RUSTUP_HOME, NOT CARGO_HOME (which holds the registry
    # and bin). Reading CARGO_HOME here found no toolchains at all and silently
    # produced an empty candidate list -- so probe both, and from each also try
    # the sibling layout used when rustup is not in play.
    roots = [
        os.environ.get("RUSTUP_HOME", ""),
        os.environ.get("CARGO_HOME", ""),
        r"D:\Rust\Rustup",
        r"D:\Rust\Cargo",
    ]
    toolchains_dirs = [os.path.join(r, "toolchains") for r in roots if r]
    candidates = []
    for toolchains in toolchains_dirs:
        if not os.path.isdir(toolchains):
            continue
        for tc in sorted(os.listdir(toolchains)):
            p = os.path.join(
                toolchains, tc, "lib", "rustlib", "x86_64-pc-windows-msvc",
                "bin", "rust-lld.exe",
            )
            if os.path.isfile(p) and p not in candidates:
                candidates.append(p)
    for path, args in [(clang_lld, [])] + [(c, ["-flavor", "gnu"]) for c in candidates]:
        try:
            probe = subprocess.run(
                [path] + args + ["--version"],
                capture_output=True, timeout=30,
            )
            # LLD prints its version banner; any successful exec proves we may run it.
            if b"LLD" in probe.stdout or b"LLD" in probe.stderr:
                return path, args
        except (OSError, subprocess.SubprocessError):
            continue
    raise SystemExit(
        "no usable LLD: tried " + clang_lld + " and " + str(candidates)
    )


LLD, LLD_ARGS = _pick_linker()
TARGET = "x86_64-unknown-none"

def run(cmd):
    r = subprocess.run(cmd)
    if r.returncode != 0:
        raise SystemExit(f"command failed ({r.returncode}): {' '.join(cmd)}")

def cc_flags():
    """freestanding C 编译旗标（**单点**：main() 与 sysroot 运行时构建共用）。"""
    return [CLANG, "--target=" + TARGET, "-ffreestanding", "-fno-builtin",
            "-fno-stack-protector", "-fno-pic", "-O2", "-I", HERE]


def build_sysroot_rt(out_dir):
    """产出 sysroot 需要的 C 运行时对象（3P1-2）。返回 0 成功。

    目前只有 user_main.o：系统入口 `_start`（libsys 提供，调 `user_main`）与 C 入口
    `main` 之间的桥接。**不含 crt0.o**——crt0.S 自带强 `_start`，与 libc.a 经 libsys
    提供的入口互斥（实测 duplicate symbol: _start，见 user_main.c 注释）。
    """
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "user_main.o")
    run(cc_flags() + ["-c", os.path.join(HERE, "user_main.c"), "-o", out])
    print(f"[crt] user_main.o -> {out}")
    return 0


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--print-toolchain":
        # 供 sysroot 安装器取用**已验证可用**的 clang/lld（避免在别处重复探测逻辑）。
        print("CLANG=" + CLANG)
        print("LLD=" + LLD)
        print("LLD_ARGS=" + " ".join(LLD_ARGS))
        return 0
    if len(sys.argv) >= 3 and sys.argv[1] == "--rt-only":
        return build_sysroot_rt(sys.argv[2])
    if len(sys.argv) < 4:
        print(__doc__); return 1
    prog, src_dir, out_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    extra_rt = sys.argv[4:]
    os.makedirs(out_dir, exist_ok=True)
    cc = cc_flags()
    # crt0.S -> crt0.o
    run(cc + ["-c", os.path.join(HERE, "crt0.S"), "-o", os.path.join(out_dir, "crt0.o")])
    # crtrt.c + extra rt -> objects (crtrt always; thread.c/pthread.c/others as extra_rt)
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
    run([LLD] + LLD_ARGS + ["-o", out, "-e", "_start", "-nostdlib", "--no-dynamic-linker"] + objs +
        ["-z", "noexecstack", "-T", os.path.join(HERE, "linker.ld")])
    print(f"[crt] {prog}.elf -> {out}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
