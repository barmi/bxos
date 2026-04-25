# cmake/toolchain-i686-elf.cmake
#
# BxOS 커널용 freestanding i386 cross 툴체인 정의.
#
# 사용:
#   cmake -S . -B build/cmake \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-i686-elf.cmake
#
# 우선순위:
#   1) i686-elf-gcc/ld/objcopy/nm        (brew install i686-elf-gcc i686-elf-binutils)
#   2) x86_64-elf-gcc/ld/objcopy/nm + -m32   (brew install x86_64-elf-gcc x86_64-elf-binutils)
#   3) 호스트 gcc/ld + -m32 (Apple Silicon clang 으로는 실패. Linux 에서나 의미 있음)
#
# 사용자가 -DBXOS_CC=... 등으로 명시적으로 넘기면 그 값을 우선 존중한다.

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR i386)

# CMake try_compile 시 cross-compiler 가 host 실행을 시도하지 않도록.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# freestanding/bare-metal 크로스 툴체인이라 CMake 의 자동 컴파일러 검사
# (test program 빌드)는 의미가 없다. 강제로 통과시킨다.
set(CMAKE_C_COMPILER_WORKS  TRUE CACHE INTERNAL "")
set(CMAKE_CXX_COMPILER_WORKS TRUE CACHE INTERNAL "")

# ─── 도구 탐지 ──────────────────────────────────────────────────────────
# 사용자가 캐시/커맨드라인으로 BXOS_CC 등을 미리 지정한 경우 그대로 사용.
function(_bxos_find_cross out_var preferred fallback last_resort)
    if(DEFINED ${out_var} AND NOT "${${out_var}}" STREQUAL "")
        return()
    endif()
    find_program(_p NAMES "${preferred}" HINTS /opt/homebrew/bin /usr/local/bin)
    if(_p)
        set(${out_var} "${_p}" PARENT_SCOPE)
        unset(_p CACHE)
        return()
    endif()
    unset(_p CACHE)
    find_program(_p NAMES "${fallback}" HINTS /opt/homebrew/bin /usr/local/bin)
    if(_p)
        set(${out_var} "${_p}" PARENT_SCOPE)
        set(BXOS_NEED_M32 TRUE PARENT_SCOPE)
        unset(_p CACHE)
        return()
    endif()
    unset(_p CACHE)
    if(last_resort)
        find_program(_p NAMES "${last_resort}" HINTS /opt/homebrew/bin /usr/local/bin)
        if(_p)
            set(${out_var} "${_p}" PARENT_SCOPE)
            set(BXOS_NEED_M32 TRUE PARENT_SCOPE)
            unset(_p CACHE)
            return()
        endif()
        unset(_p CACHE)
    endif()
endfunction()

set(BXOS_NEED_M32 FALSE)

_bxos_find_cross(BXOS_CC      i686-elf-gcc     x86_64-elf-gcc     gcc)
_bxos_find_cross(BXOS_LD      i686-elf-ld      x86_64-elf-ld      ld)
_bxos_find_cross(BXOS_OBJCOPY i686-elf-objcopy x86_64-elf-objcopy objcopy)
_bxos_find_cross(BXOS_NM      i686-elf-nm      x86_64-elf-nm      nm)

if(NOT BXOS_CC)
    message(FATAL_ERROR
        "i686-elf-gcc / x86_64-elf-gcc / gcc 를 찾을 수 없습니다.\n"
        "  brew install i686-elf-gcc i686-elf-binutils\n"
        "또는\n"
        "  brew install x86_64-elf-gcc x86_64-elf-binutils\n"
        "를 실행한 뒤 다시 시도하세요."
    )
endif()

# CMake 가 컴파일러 식별 단계를 수행할 때 freestanding 인자를 강제.
set(CMAKE_C_COMPILER       "${BXOS_CC}"      CACHE FILEPATH "C compiler" FORCE)
set(CMAKE_LINKER           "${BXOS_LD}"      CACHE FILEPATH "Linker"     FORCE)
set(CMAKE_OBJCOPY          "${BXOS_OBJCOPY}" CACHE FILEPATH "objcopy"    FORCE)
set(CMAKE_NM               "${BXOS_NM}"      CACHE FILEPATH "nm"         FORCE)

# Apple Silicon clang/cc 는 i386 코드 생성이 안 되니 시스템 cc 를 폴백 컴파일러로
# 잡았다면 즉시 경고 (사실상 실패한다).
if(BXOS_NEED_M32 AND CMAKE_C_COMPILER MATCHES "/(cc|gcc)$"
   AND NOT CMAKE_C_COMPILER MATCHES "elf-")
    message(WARNING
        "i686-elf / x86_64-elf 크로스 컴파일러를 못 찾아 시스템 ${CMAKE_C_COMPILER} 로 "
        "폴백했습니다. macOS Apple Silicon 에서는 -m32 코드 생성이 거의 항상 실패합니다. "
        "brew install i686-elf-gcc i686-elf-binutils 를 권장합니다."
    )
endif()

# ─── 컴파일/링크 기본 플래그 ────────────────────────────────────────────
# Makefile.modern 의 CFLAGS 와 동일한 의미. 자세한 주석은 그쪽 참조.
set(_BXOS_FREESTANDING_FLAGS
    "-ffreestanding -fno-pic -fno-pie -fno-stack-protector"
    "-fno-asynchronous-unwind-tables -fno-builtin -nostdlib"
    "-fleading-underscore"
    "-fno-common"
    "-Wall"
    "-Wno-implicit-function-declaration -Wno-int-conversion"
    "-Wno-pointer-sign -Wno-incompatible-pointer-types"
)
string(REPLACE ";" " " _BXOS_FREESTANDING_FLAGS "${_BXOS_FREESTANDING_FLAGS}")

if(BXOS_NEED_M32)
    set(_BXOS_FREESTANDING_FLAGS "-m32 ${_BXOS_FREESTANDING_FLAGS}")
endif()
# i686-elf-gcc 도 -m32 추가해 두면 무해 (기본 32-bit).
set(_BXOS_FREESTANDING_FLAGS "-m32 ${_BXOS_FREESTANDING_FLAGS}")

set(CMAKE_C_FLAGS_INIT          "${_BXOS_FREESTANDING_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG_INIT    "-O0 -g")
set(CMAKE_C_FLAGS_RELEASE_INIT  "-O2")

# CMake 가 자체적으로 시스템 헤더/라이브러리를 추가하지 못하게.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
