# The libretro frontend: a fourth one beside qt, android and ios.
#
# It is a shared object the frontend loads, so everything it links has to be
# position independent - CMAKE_POSITION_INDEPENDENT_CODE is forced ON for the
# whole tree when this is enabled (see the root CMakeLists), since the emulator
# libraries are static and built for an executable otherwise.

add_library(eka2l1_libretro SHARED
    src/main.cpp
    src/content.cpp
    src/hooks.cpp
    src/state.cpp
    src/thread.cpp
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
    epocutils
    epockern
    epocpkg
    gdbstub
    uvw
    j2me
    stb
    yaml-cpp)

# Libretro cores are named <core>_libretro.so, with no "lib" prefix.
# The version reported is upstream's, with the upstream commit this core is
# rebased on, from upstream.version
file(STRINGS "${PROJECT_SOURCE_DIR}/upstream.version" EKA2L1_UPSTREAM_COMMIT LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/upstream.version")
target_compile_definitions(eka2l1_libretro PRIVATE
    EKA2L1_UPSTREAM_COMMIT="${EKA2L1_UPSTREAM_COMMIT}")

# RetroArch's names: eka2l1_libretro_android.so on Android
if (ANDROID)
    set(EKA2L1_LIBRETRO_NAME eka2l1_libretro_android)
else()
    set(EKA2L1_LIBRETRO_NAME eka2l1_libretro)
endif()

set_target_properties(eka2l1_libretro PROPERTIES
    OUTPUT_NAME ${EKA2L1_LIBRETRO_NAME}
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")

# On Linux and Android the core binds its own symbols inside itself: the
# frontend's libraries do not stand in for them, and FFmpeg's x86 assembly,
# which refers to its tables directly, links into a shared object
if (UNIX AND NOT APPLE)
    target_link_options(eka2l1_libretro PRIVATE -Wl,-Bsymbolic)
endif()

# The GL loader asks EGL for the functions on Android
if (ANDROID)
    target_link_libraries(eka2l1_libretro PRIVATE EGL GLESv3 log android)
endif()
