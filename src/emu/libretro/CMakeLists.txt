# The libretro frontend: a fourth one beside qt, android and ios.
#
# It is a shared object the frontend loads, so everything it links has to be
# position independent - CMAKE_POSITION_INDEPENDENT_CODE is forced ON for the
# whole tree when this is enabled (see the root CMakeLists), since the emulator
# libraries are static and built for an executable otherwise.

add_library(eka2l1_libretro SHARED
    src/main.cpp
    src/state.cpp
    include/libretro.h
    include/libretro_state.h)

target_include_directories(eka2l1_libretro PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

# GIT_BRANCH / GIT_COMMIT_HASH and the rest come from the generated headers the
# emulator libraries already carry.

# The same set the Qt frontend links, minus Qt itself.
target_link_libraries(eka2l1_libretro PRIVATE
    common
    config
    drivers
    epoc
    epocservs
    epockern
    epocpkg
    gdbstub
    uvw
    j2me
    stb
    yaml-cpp)

# Libretro cores are named <core>_libretro.so, with no "lib" prefix.
set_target_properties(eka2l1_libretro PROPERTIES
    OUTPUT_NAME eka2l1_libretro
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
