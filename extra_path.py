from pathlib import Path

Import("env")

platform = env.PioPlatform()
toolchain = Path(platform.get_package_dir("toolchain-riscv32-esp"))
compiler_bin = toolchain / "riscv32-esp-elf" / "bin"

if compiler_bin.exists():
    env.PrependENVPath("PATH", str(compiler_bin))
