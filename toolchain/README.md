# CykOS GCC user application toolchain

This provides an `i686-cykos-gcc` compiler driver around an installed GCC and
builds the CykOS libc sysroot. It targets the current 32-bit i386 user ELF ABI;
it does not build GCC or Binutils from source. `i686-elf-gcc` is preferred,
then `i686-linux-gnu-gcc`, then host `gcc -m32`.

From the repository root:

```sh
./toolchain/build.sh
export PATH="$PWD/toolchain/bin:$PATH"
i686-cykos-gcc -O2 -Wall -Wextra -o hello.elf hello.c
```

The driver supplies the CykOS headers, freestanding compile settings, startup
code, libc, and the static user ELF linker script. `-c`, `-S`, and `-E` stop
before linking, so they can be used for ordinary compile-only steps. The
default linked output is a static CykOS user executable; pass `-o` to choose
its filename. Copy the resulting ELF into `/USR/BIN` on the image to run it.
The sysroot also provides `libm.a`, so conventional `-lm` links are available.

The sysroot is generated under `build/cykos-sysroot`. Re-run `toolchain/build.sh`
after changing libc headers or sources. Dynamic linking, shared libraries, and
host POSIX compatibility layers are not provided by this toolchain yet.
