#!/usr/bin/env bash
# Stage a CykOS cross-compilation sysroot for building Binutils & GCC
# targeting i686-elf / i686-cykos with CykOS libc and high-address linker script.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SYSROOT="${ROOT_DIR}/toolchain/sysroot"

mkdir -p "${SYSROOT}/usr/include" "${SYSROOT}/usr/lib"

cp -r "${ROOT_DIR}/libc/include/." "${SYSROOT}/usr/include/"
if [[ -f "${ROOT_DIR}/bin/libc.a" ]]; then
    cp "${ROOT_DIR}/bin/libc.a" "${SYSROOT}/usr/lib/libc.a"
fi
if [[ -f "${ROOT_DIR}/bin/libc_objs/crt0.o" ]]; then
    cp "${ROOT_DIR}/bin/libc_objs/crt0.o" "${SYSROOT}/usr/lib/crt0.o"
fi
cp "${ROOT_DIR}/toolchain/cykos-high.ld" "${SYSROOT}/usr/lib/cykos-high.ld"

echo "[sysroot] Staged CykOS GCC sysroot at ${SYSROOT}"
echo "[sysroot] Link large CykOS binaries with:"
echo "  i686-elf-gcc -m32 -ffreestanding -nostdlib -I${SYSROOT}/usr/include \\"
echo "    -T ${SYSROOT}/usr/lib/cykos-high.ld \\"
echo "    ${SYSROOT}/usr/lib/crt0.o <objs...> ${SYSROOT}/usr/lib/libc.a -lgcc"
