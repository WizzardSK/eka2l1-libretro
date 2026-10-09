#!/bin/sh -ex

# Cross-builds the libretro core for Windows x64 on Linux, with llvm-mingw
# (clang, lld, libc++ and mingw-w64), the way rpcs3-libretro does on the
# libretro buildbot: its own Windows runner has MSVC 2019 (19.28), too old for
# the C++20 the emulator is written in, and its MXE image is a GCC toolchain
# EKA2L1 is not built with. FFmpeg comes from its submodule, configured for
# mingw32 by src/external/ffmpeg's CMake.
#
# Expects to run from the top of the source tree, in the mstorsjo/llvm-mingw
# image.

TRIPLE=x86_64-w64-mingw32
SRC="$PWD"
DEPS="$SRC/deps-$TRIPLE"
JOBS="${NUMPROC:-$(nproc)}"
[ "$JOBS" -ge 1 ] || JOBS=1
BUILD_DIR="${BUILD_DIR:-build/windows-x86_64}"

git config --global --add safe.directory '*'
git submodule -q update --init --recursive --force --depth 1

mkdir -p "$DEPS"
cat > "$DEPS/toolchain.cmake" <<TC
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $TRIPLE-clang)
set(CMAKE_CXX_COMPILER $TRIPLE-clang++)
set(CMAKE_RC_COMPILER $TRIPLE-windres)
set(CMAKE_AR llvm-ar)
set(CMAKE_RANLIB llvm-ranlib)
set(CMAKE_NM llvm-nm)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
TC

# Windows headers and libraries as the source spells them: MSVC sits on a
# case-insensitive file system, so <Windows.h> works there; mingw-w64 names its
# headers in lower case. Each mixed-case include whose lower-case name is a
# mingw-w64 header gets a link under that spelling, searched last.
MINGW_INC="$(echo | $TRIPLE-clang -E -x c - -v 2>&1 | sed -n '/<...> search starts here/,/End of search list/p' | sed 's/^ *//' | grep -i 'mingw32/include$' | head -1)"
test -d "$MINGW_INC"
CASE_INC="$DEPS/case-include"
mkdir -p "$CASE_INC"
grep -rhoE '#[[:space:]]*include[[:space:]]*[<"][A-Za-z0-9_]*[A-Z][A-Za-z0-9_]*\.h[>"]' \
    --include='*.c' --include='*.cpp' --include='*.h' --include='*.hpp' --include='*.inl' \
    --exclude-dir=build --exclude-dir="deps-$TRIPLE" src |
    sed -E 's/.*[<"]([^>"]*)[>"].*/\1/' | sort -u |
    while read -r h; do
        lc="$(echo "$h" | tr 'A-Z' 'a-z')"
        if [ ! -e "$MINGW_INC/$h" ] && [ -e "$MINGW_INC/$lc" ]; then
            ln -sf "$MINGW_INC/$lc" "$CASE_INC/$h"
        fi
    done
ls "$CASE_INC"

# The same for libraries linked by a mixed-case name (Ws2_32, Dbghelp...).
MINGW_LIB="$(dirname "$MINGW_INC")/lib"
test -d "$MINGW_LIB"
CASE_LIB="$DEPS/case-lib"
mkdir -p "$CASE_LIB"
find src -name CMakeLists.txt -not -path "./deps-$TRIPLE/*" -exec cat {} + |
    grep -oE '[A-Za-z0-9_]*[A-Z][A-Za-z0-9_]*' | sort -u |
    while read -r l; do
        lc="$(echo "$l" | tr 'A-Z' 'a-z')"
        if [ -e "$MINGW_LIB/lib$lc.a" ] && [ ! -e "$MINGW_LIB/lib$l.a" ]; then
            ln -sf "$MINGW_LIB/lib$lc.a" "$CASE_LIB/lib$l.a"
        fi
    done
ls "$CASE_LIB"

# zlib for FFmpeg (it is configured with --enable-zlib, and mingw-w64 has no
# zlib of its own), from the submodule the emulator builds its own copy from
if [ ! -f "$DEPS/lib/libz.a" ]; then
    mkdir -p "$DEPS/include" "$DEPS/lib" "$DEPS/zlib-obj"
    for f in adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback \
             inffast inflate inftrees trees uncompr zutil; do
        $TRIPLE-clang -O2 -c src/external/zlib/$f.c -o "$DEPS/zlib-obj/$f.o"
    done
    llvm-ar rcs "$DEPS/lib/libz.a" "$DEPS"/zlib-obj/*.o
    cp src/external/zlib/zlib.h src/external/zlib/zconf.h "$DEPS/include/"
fi

# std::unary_function, which the vendored Boost still uses, is gone from
# libc++ in C++17 and later unless asked for
LIBCXX_COMPAT="-D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION"

cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DEPS/toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCI=ON -DEKA2L1_BUILD_LIBRETRO=ON -DEKA2L1_SCRIPTING_LUA=OFF \
    -DCMAKE_C_FLAGS="-I$DEPS/include -idirafter $CASE_INC" \
    -DCMAKE_CXX_FLAGS="$LIBCXX_COMPAT -idirafter $CASE_INC" \
    -DCMAKE_EXE_LINKER_FLAGS="-L$DEPS/lib" \
    -DCMAKE_SHARED_LINKER_FLAGS="-static -L$CASE_LIB -L$DEPS/lib"
if ! cmake --build "$BUILD_DIR" --target eka2l1_libretro -j "$JOBS" -- ${KEEP_GOING:+-k 0}; then
    # FFmpeg's configure log says why it gave up, the build's own output does not
    for log in "$BUILD_DIR"/../ffmpeg-cache/*/build.log build/ffmpeg-cache/*/build.log; do
        [ -f "$log" ] && tail -n 60 "$log"
    done
    exit 1
fi
