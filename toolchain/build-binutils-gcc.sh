#!/usr/bin/env bash
# Cross-compile GNU Binutils & GCC natively for CykOS (i686 ELF32 high-VM).
# Builds in a space-free native Linux directory ($HOME/.cache/cykos-toolchain)
# because GNU autotools rejects paths with spaces, then copies the stripped
# result into toolchain/root/usr/ for makeext2.py.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TC_DIR="${ROOT_DIR}/toolchain"
CACHE_SRC="${TC_DIR}/src"
OUT_ROOT="${TC_DIR}/root"

WORK_DIR="${CYKOS_TC_WORK:-${HOME}/.cache/cykos-toolchain}"
SRC_DIR="${WORK_DIR}/src"
BUILD_DIR="${WORK_DIR}/build"
STAGE_ROOT="${WORK_DIR}/root"
SYSROOT="${WORK_DIR}/sysroot"

BINUTILS_VER="${BINUTILS_VER:-2.42}"
GCC_VER="${GCC_VER:-13.2.0}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

# 1. Stage space-free CykOS sysroot & compiler wrapper
bash "${TC_DIR}/build-gcc-sysroot.sh"
mkdir -p "${SRC_DIR}" "${BUILD_DIR}" "${STAGE_ROOT}/usr/bin" "${WORK_DIR}/bin"
rm -rf "${SYSROOT}"
cp -a "${TC_DIR}/sysroot" "${SYSROOT}"
cp "${TC_DIR}/i686-cykos-gcc" "${WORK_DIR}/bin/i686-cykos-gcc"
chmod +x "${WORK_DIR}/bin/i686-cykos-gcc"

export CYKOS_SYSROOT="${SYSROOT}"
export CC="${WORK_DIR}/bin/i686-cykos-gcc"
export CC_FOR_BUILD="${CC_FOR_BUILD:-gcc}"
export CXX_FOR_BUILD="${CXX_FOR_BUILD:-g++}"
if command -v i686-elf-ar &>/dev/null; then
    export AR="i686-elf-ar" RANLIB="i686-elf-ranlib" STRIP="i686-elf-strip"
elif command -v x86_64-linux-gnu-ar &>/dev/null; then
    export AR="x86_64-linux-gnu-ar" RANLIB="x86_64-linux-gnu-ranlib" STRIP="x86_64-linux-gnu-strip"
else
    export AR="${AR:-ar}" RANLIB="${RANLIB:-ranlib}" STRIP="${STRIP:-strip}"
fi
export CFLAGS="-O2"
export LDFLAGS=""

mkdir -p "${CACHE_SRC}"

download_and_extract() {
    local name="$1" url="$2"
    local tarball="$(basename "$2")"
    local archive="${CACHE_SRC}/${tarball}"
    if [[ ! -d "${SRC_DIR}/${name}" ]]; then
        if [[ ! -f "$archive" ]]; then
            echo "[toolchain] Downloading ${url}..."
            curl -fL "$url" -o "$archive"
        fi
        echo "[toolchain] Extracting ${tarball} into ${SRC_DIR}..."
        tar -xf "$archive" -C "${SRC_DIR}"
    fi
}

# 2. Build Binutils (as, ld, ar, ranlib, nm, objdump, strip)
download_and_extract "binutils-${BINUTILS_VER}" \
    "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VER}.tar.xz"

mkdir -p "${BUILD_DIR}/binutils"
cd "${BUILD_DIR}/binutils"
if [[ ! -f Makefile ]]; then
    "${SRC_DIR}/binutils-${BINUTILS_VER}/configure" \
        --host=i686-elf \
        --target=i686-elf \
        --prefix=/usr \
        --disable-nls \
        --disable-werror \
        --disable-shared \
        --enable-static \
        --disable-gprofng \
        --disable-gdb \
        --with-sysroot=/
fi
make -j"${JOBS}" all-binutils all-gas all-ld
make DESTDIR="${STAGE_ROOT}" install-binutils install-gas install-ld

# 3. Build GCC (C compiler: gcc, cc1, collect2, libgcc)
download_and_extract "gcc-${GCC_VER}" \
    "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.xz"

if [[ ! -d "${SRC_DIR}/gcc-${GCC_VER}/gmp" ]]; then
    echo "[toolchain] Downloading GCC prerequisites (GMP, MPFR, MPC)..."
    (cd "${SRC_DIR}/gcc-${GCC_VER}" && ./contrib/download_prerequisites)
fi

mkdir -p "${BUILD_DIR}/gcc"
cd "${BUILD_DIR}/gcc"
if [[ ! -f Makefile ]]; then
    "${SRC_DIR}/gcc-${GCC_VER}/configure" \
        --host=i686-elf \
        --target=i686-elf \
        --prefix=/usr \
        --enable-languages=c \
        --disable-nls \
        --disable-shared \
        --disable-threads \
        --disable-libssp \
        --disable-libgomp \
        --disable-libquadmath \
        --disable-libatomic \
        --disable-multilib \
        --without-headers \
        --with-sysroot=/ \
        --with-native-system-header-dir=/usr/include
fi
make -j"${JOBS}" all-gcc all-target-libgcc
make DESTDIR="${STAGE_ROOT}" install-gcc install-target-libgcc

# 4. Strip large binaries and copy staged toolchain into toolchain/root/
find "${STAGE_ROOT}/usr/bin" "${STAGE_ROOT}/usr/libexec" -type f -executable 2>/dev/null | while read -r bin; do
    "$STRIP" --strip-unneeded "$bin" 2>/dev/null || true
done

rm -rf "${OUT_ROOT}"
mkdir -p "${OUT_ROOT}"
cp -a "${STAGE_ROOT}/usr" "${OUT_ROOT}/usr"

echo "[toolchain] Native Binutils & GCC staged in ${OUT_ROOT}/usr"
echo "[toolchain] Run 'make rebuild' (auto-sizes image to 256MB) to bundle into CykOS!"
