# CMake toolchain for cross-compiling plain C/C++ libraries to the Nintendo 3DS
# (ARM11 / ARMv6k) with devkitARM, for linking into a 3GX plugin.
#
#   cmake -S <src> -B <build> -DCMAKE_TOOLCHAIN_FILE=cmake/3DSToolchain.cmake
#
# This deliberately does NOT use devkitPro's own 3DS.cmake. That one targets
# homebrew applications: it links libctru, expects a main(), and produces
# .3dsx/.elf executables. We want a freestanding static library whose flags
# match the plugin's, because a 3GX plugin is linked with its own script and
# cannot pick up devkitPro's application-level assumptions.
#
# Modelled on the devkitPPC Wii toolchain used for the same library previously.

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ---- Locate devkitPro -------------------------------------------------------

if(NOT DEFINED DEVKITPRO)
    if(DEFINED ENV{DEVKITPRO})
        set(DEVKITPRO $ENV{DEVKITPRO})
    elseif(EXISTS "/opt/devkitpro/devkitARM")
        set(DEVKITPRO "/opt/devkitpro")
    elseif(EXISTS "C:/devkitPro/devkitARM")
        set(DEVKITPRO "C:/devkitPro")
    else()
        message(FATAL_ERROR "DEVKITPRO is not set and devkitPro was not found.")
    endif()
endif()

set(DEVKITARM "${DEVKITPRO}/devkitARM")

if(NOT EXISTS "${DEVKITARM}")
    message(FATAL_ERROR "devkitARM not found at ${DEVKITARM}")
endif()

if(CMAKE_HOST_WIN32)
    set(TOOL_SUFFIX ".exe")
else()
    set(TOOL_SUFFIX "")
endif()

# ---- Compilers --------------------------------------------------------------

set(CMAKE_C_COMPILER   "${DEVKITARM}/bin/arm-none-eabi-gcc${TOOL_SUFFIX}"     CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "${DEVKITARM}/bin/arm-none-eabi-g++${TOOL_SUFFIX}"     CACHE FILEPATH "")
set(CMAKE_ASM_COMPILER "${DEVKITARM}/bin/arm-none-eabi-gcc${TOOL_SUFFIX}"     CACHE FILEPATH "")
set(CMAKE_AR           "${DEVKITARM}/bin/arm-none-eabi-ar${TOOL_SUFFIX}"      CACHE FILEPATH "")
set(CMAKE_RANLIB       "${DEVKITARM}/bin/arm-none-eabi-ranlib${TOOL_SUFFIX}"  CACHE FILEPATH "")
set(CMAKE_STRIP        "${DEVKITARM}/bin/arm-none-eabi-strip${TOOL_SUFFIX}"   CACHE FILEPATH "")
set(CMAKE_OBJCOPY      "${DEVKITARM}/bin/arm-none-eabi-objcopy${TOOL_SUFFIX}" CACHE FILEPATH "")

# There is no OS to link against, so CMake's default "compile and run a test
# executable" probe cannot work. Build a static library instead.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ---- Flags ------------------------------------------------------------------
#
# These must match plugin/Makefile exactly. A mismatch in float ABI or thread
# pointer model produces link errors that are painful to diagnose.

set(ARCH_FLAGS "-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mlittle-endian")

# -Os over -O2: plugin memory is the binding constraint, not CPU headroom.
# -ffunction-sections/-fdata-sections let the plugin link drop everything in
# eSpeak we never call, which is most of it.
set(COMMON_FLAGS "${ARCH_FLAGS} -Os -mword-relocations -fomit-frame-pointer \
-ffunction-sections -fdata-sections -fno-strict-aliasing -D__3DS__ -DARM11 -D_3DS")

set(CMAKE_C_FLAGS_INIT   "${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${COMMON_FLAGS} -fno-rtti -fno-exceptions")
set(CMAKE_ASM_FLAGS_INIT "${ARCH_FLAGS}")

set(CMAKE_C_FLAGS   "${COMMON_FLAGS}"                          CACHE STRING "")
set(CMAKE_CXX_FLAGS "${COMMON_FLAGS} -fno-rtti -fno-exceptions" CACHE STRING "")
set(CMAKE_ASM_FLAGS "${ARCH_FLAGS}"                            CACHE STRING "")

# ---- Include paths ----------------------------------------------------------

include_directories(SYSTEM "${DEVKITPRO}/libctru/include")
if(EXISTS "${DEVKITPRO}/portlibs/3ds/include")
    include_directories(SYSTEM "${DEVKITPRO}/portlibs/3ds/include")
endif()

link_directories("${DEVKITPRO}/libctru/lib")

# ---- Find-root behaviour ----------------------------------------------------

set(CMAKE_FIND_ROOT_PATH "${DEVKITARM}" "${DEVKITPRO}/libctru" "${DEVKITPRO}/portlibs/3ds")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ---- Capability declarations ------------------------------------------------
#
# The 3DS is a freestanding target: no dynamic linking, no fork/exec, no
# terminal. Telling CMake up front stops configure checks from probing for
# them and guessing wrong.

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF CACHE BOOL "" FORCE)
set(THREADS_PREFER_PTHREAD_FLAG OFF CACHE BOOL "" FORCE)

set(3DS TRUE)
set(NINTENDO_3DS TRUE)
