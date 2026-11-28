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
GCC_VER="${GCC_VER:-4.6.4}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

# 1. Ensure libc.a + crt0.o exist and stage space-free CykOS sysroot & compiler wrapper
if [[ ! -f "${ROOT_DIR}/bin/libc.a" || ! -f "${ROOT_DIR}/bin/libc_objs/crt0.o" ]]; then
    make -C "${ROOT_DIR}" -j"${JOBS}" bin/libc.a bin/libc_objs/crt0.o
fi
bash "${TC_DIR}/build-gcc-sysroot.sh"
mkdir -p "${SRC_DIR}" "${BUILD_DIR}" "${STAGE_ROOT}/usr/bin" "${WORK_DIR}/bin"
rm -rf "${SYSROOT}"
cp -a "${TC_DIR}/sysroot" "${SYSROOT}"
cp "${TC_DIR}/i686-cykos-gcc" "${WORK_DIR}/bin/i686-cykos-gcc"
chmod +x "${WORK_DIR}/bin/i686-cykos-gcc"

export CYKOS_SYSROOT="${SYSROOT}"
export CC="${WORK_DIR}/bin/i686-cykos-gcc"
BUILD_CC_RAW="${CC_FOR_BUILD:-gcc}"
cat > "${WORK_DIR}/bin/build-cc" <<EOF
#!/usr/bin/env bash
exec ${BUILD_CC_RAW} -std=gnu89 -Wno-implicit-function-declaration -Wno-implicit-int -Wno-return-mismatch "\$@"
EOF
chmod +x "${WORK_DIR}/bin/build-cc"
export CC_FOR_BUILD="${WORK_DIR}/bin/build-cc"
export CXX_FOR_BUILD="${CXX_FOR_BUILD:-g++}"

# Provide host i686-elf-{as,ld,ar,ranlib} wrappers for building target libgcc
if ! command -v i686-elf-as &>/dev/null && command -v x86_64-linux-gnu-as &>/dev/null; then
    cat > "${WORK_DIR}/bin/i686-elf-as" <<'EOF'
#!/usr/bin/env bash
exec x86_64-linux-gnu-as --32 "$@"
EOF
    cat > "${WORK_DIR}/bin/i686-elf-ld" <<'EOF'
#!/usr/bin/env bash
exec x86_64-linux-gnu-ld -m elf_i386 "$@"
EOF
    chmod +x "${WORK_DIR}/bin/i686-elf-as" "${WORK_DIR}/bin/i686-elf-ld"
fi
if ! command -v i686-elf-ar &>/dev/null && command -v x86_64-linux-gnu-ar &>/dev/null; then
    for tool in ar ranlib nm strip objdump; do
        ln -sf "$(command -v "x86_64-linux-gnu-${tool}")" "${WORK_DIR}/bin/i686-elf-${tool}"
    done
fi
export PATH="${WORK_DIR}/bin:${PATH}"
ln -sf i686-cykos-gcc "${WORK_DIR}/bin/i686-elf-cc"
export CC_FOR_TARGET="${WORK_DIR}/bin/i686-cykos-gcc"
export GCC_FOR_TARGET="${WORK_DIR}/bin/i686-cykos-gcc"
export AS_FOR_TARGET="${WORK_DIR}/bin/i686-elf-as"
export LD_FOR_TARGET="${WORK_DIR}/bin/i686-elf-ld"
if ! command -v i686-elf-as &>/dev/null; then
    export gcc_cv_as="${WORK_DIR}/bin/i686-elf-as"
    export gcc_cv_ld="${WORK_DIR}/bin/i686-elf-ld"
fi
export ac_cv_func_strncmp=yes

if command -v i686-elf-ar &>/dev/null; then
    export AR="i686-elf-ar" RANLIB="i686-elf-ranlib" STRIP="i686-elf-strip"
elif command -v x86_64-linux-gnu-ar &>/dev/null; then
    export AR="x86_64-linux-gnu-ar" RANLIB="x86_64-linux-gnu-ranlib" STRIP="x86_64-linux-gnu-strip"
else
    export AR="${AR:-ar}" RANLIB="${RANLIB:-ranlib}" STRIP="${STRIP:-strip}"
fi
export CFLAGS="-O2 -std=gnu89 -Wno-implicit-function-declaration"
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
if [[ -f binutils/config.h ]] && ! grep -q '^#define HAVE_UTIME_H 1' binutils/config.h; then
    echo "[toolchain] Refreshing binutils configure cache for new libc headers..."
    rm -f config.cache */config.cache binutils/Makefile binutils/config.h
fi
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
make -j"${JOBS}" MAKEINFO=true all-binutils all-gas all-ld
make DESTDIR="${STAGE_ROOT}" MAKEINFO=true install-binutils install-gas install-ld

# 3. Build GCC (C compiler: gcc, cc1, collect2, libgcc)
GCC_EXT="tar.xz"
case "${GCC_VER}" in
    4.[0-7].*) GCC_EXT="tar.bz2" ;;
esac
download_and_extract "gcc-${GCC_VER}" \
    "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.${GCC_EXT}"

if [[ ! -d "${SRC_DIR}/gcc-${GCC_VER}/gmp" ]]; then
    echo "[toolchain] Downloading GCC prerequisites (GMP, MPFR, MPC) over HTTPS..."
    for pkg in gmp-4.3.2.tar.bz2 mpfr-2.4.2.tar.bz2 mpc-0.8.1.tar.gz; do
        if [[ ! -f "${CACHE_SRC}/${pkg}" ]]; then
            curl -fL "https://gcc.gnu.org/pub/gcc/infrastructure/${pkg}" -o "${CACHE_SRC}/${pkg}"
        fi
        tar -xf "${CACHE_SRC}/${pkg}" -C "${SRC_DIR}/gcc-${GCC_VER}"
    done
    (
        cd "${SRC_DIR}/gcc-${GCC_VER}"
        ln -sfn gmp-4.3.2 gmp
        ln -sfn mpfr-2.4.2 mpfr
        ln -sfn mpc-0.8.1 mpc
    )
fi

if [[ -f "${SRC_DIR}/binutils-${BINUTILS_VER}/config.sub" ]]; then
    for d in "${SRC_DIR}/gcc-${GCC_VER}" "${SRC_DIR}/gcc-${GCC_VER}/gmp-4.3.2" "${SRC_DIR}/gcc-${GCC_VER}/mpfr-2.4.2" "${SRC_DIR}/gcc-${GCC_VER}/mpc-0.8.1"; do
        [[ -d "$d" ]] || continue
        cp -f "${SRC_DIR}/binutils-${BINUTILS_VER}/config.sub" "$d/config.sub"
        cp -f "${SRC_DIR}/binutils-${BINUTILS_VER}/config.guess" "$d/config.guess"
    done
fi

mkdir -p "${BUILD_DIR}/gcc"
cd "${BUILD_DIR}/gcc"
BUILD_TRIPLET="$("${BUILD_CC_RAW}" -dumpmachine)"
FLAGS_STAMP="${CC_FOR_BUILD}|${CFLAGS}|${AS_FOR_TARGET}"
if [[ ! -f .flags-stamp ]] || [[ "$(cat .flags-stamp 2>/dev/null || true)" != "${FLAGS_STAMP}" ]]; then
    echo "[toolchain] Resetting gcc build directory for updated flags..."
    rm -rf "${BUILD_DIR}/gcc"/*
    printf '%s' "${FLAGS_STAMP}" > .flags-stamp
fi
if [[ ! -f Makefile ]]; then
    "${SRC_DIR}/gcc-${GCC_VER}/configure" \
        --build="${BUILD_TRIPLET}" \
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
make -j"${JOBS}" MAKEINFO=true CC_FOR_TARGET="${WORK_DIR}/bin/i686-cykos-gcc" GCC_FOR_TARGET="${WORK_DIR}/bin/i686-cykos-gcc" all-gcc all-target-libgcc
make DESTDIR="${STAGE_ROOT}" MAKEINFO=true CC_FOR_TARGET="${WORK_DIR}/bin/i686-cykos-gcc" GCC_FOR_TARGET="${WORK_DIR}/bin/i686-cykos-gcc" install-gcc install-target-libgcc

# 4. Ensure unprefixed tool binaries exist and strip debug symbols
for tool in gcc gcov cpp as ld ar ranlib nm objdump objcopy readelf size strings strip; do
    if [[ ! -e "${STAGE_ROOT}/usr/bin/${tool}" && -e "${STAGE_ROOT}/usr/bin/i686-elf-${tool}" ]]; then
        cp -a "${STAGE_ROOT}/usr/bin/i686-elf-${tool}" "${STAGE_ROOT}/usr/bin/${tool}"
    fi
done

find "${STAGE_ROOT}/usr/bin" "${STAGE_ROOT}/usr/libexec" -type f -executable 2>/dev/null | while read -r bin; do
    "$STRIP" --strip-unneeded "$bin" 2>/dev/null || true
done

rm -rf "${OUT_ROOT}"
mkdir -p "${OUT_ROOT}"
cp -a "${STAGE_ROOT}/usr" "${OUT_ROOT}/usr"

echo "[toolchain] Native Binutils & GCC staged in ${OUT_ROOT}/usr"
echo "[toolchain] Run 'make rebuild' (auto-sizes image to 256MB) to bundle into CykOS!"
