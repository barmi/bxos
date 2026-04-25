# CMakeLists.txt — BxOS (Wine 없는 modern 빌드의 CMake 버전)
#
# 사용:
#   cmake -S . -B build/cmake
#   # (기본적으로 cmake/toolchain-i686-elf.cmake 가 사용됩니다)
#
#   cmake --build build/cmake               # 전체 (haribote.img)
#   cmake --build build/cmake --target kernel
#   cmake --build build/cmake --target run  # qemu 부팅
#
# 동작은 tools/modern/Makefile.modern 와 동일하다:
#   - 커널(haribote.sys) 은 NASM + i686-elf-gcc + Python 으로 새로 빌드.
#   - 앱(.hrb) 은 harib27f/<app>/<app>.hrb 의 기존 빌드본을 그대로 IMG 에 포함.
#
# build-modern.sh / Makefile.modern 도 그대로 둔다 (둘 다 사용 가능).

cmake_minimum_required(VERSION 3.20)

# ─── 기본 툴체인 설정 ──────────────────────────────────────────────────
# CLion 등 IDE에서 기본 툴체인을 i686-elf 로 강제하기 위해 설정합니다.
if(NOT CMAKE_TOOLCHAIN_FILE OR CMAKE_TOOLCHAIN_FILE STREQUAL "")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain-i686-elf.cmake" CACHE FILEPATH "Default toolchain file" FORCE)
endif()

project(bxos
    LANGUAGES C
    DESCRIPTION "BxOS — modern toolchain build of haribote-derived OS"
)

# ─── 경로 ────────────────────────────────────────────────────────────
set(BXOS_TOOLS_DIR  "${CMAKE_SOURCE_DIR}/tools/modern")
set(BXOS_KSRC       "${CMAKE_SOURCE_DIR}/harib27f/haribote")
set(BXOS_HARIB      "${CMAKE_SOURCE_DIR}/harib27f")
set(BXOS_ZINC       "${CMAKE_SOURCE_DIR}/z_tools/haribote")
set(BXOS_BUILD      "${CMAKE_BINARY_DIR}")  # build/cmake/

# ─── 외부 도구 ───────────────────────────────────────────────────────
find_program(BXOS_NASM   NAMES nasm REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(BXOS_PYTHON ${Python3_EXECUTABLE})

set(BXOS_NAS2NASM "${BXOS_TOOLS_DIR}/nas2nasm.py")
set(BXOS_HRBIFY   "${BXOS_TOOLS_DIR}/hrbify.py")
set(BXOS_MAKEFONT "${BXOS_TOOLS_DIR}/makefont.py")
set(BXOS_MKFAT12  "${BXOS_TOOLS_DIR}/mkfat12.py")
set(BXOS_LDS      "${BXOS_TOOLS_DIR}/linker-bootpack.lds")

foreach(_t IN ITEMS
        "${BXOS_NAS2NASM}" "${BXOS_HRBIFY}" "${BXOS_MAKEFONT}"
        "${BXOS_MKFAT12}"  "${BXOS_LDS}")
    if(NOT EXISTS "${_t}")
        message(FATAL_ERROR "missing tool: ${_t}")
    endif()
endforeach()

# Toolchain 파일이 잡아줘야 하지만 안전망.
if(NOT BXOS_LD)
    find_program(BXOS_LD      NAMES i686-elf-ld      x86_64-elf-ld      ld      REQUIRED)
endif()
if(NOT BXOS_OBJCOPY)
    find_program(BXOS_OBJCOPY NAMES i686-elf-objcopy x86_64-elf-objcopy objcopy REQUIRED)
endif()
if(NOT BXOS_NM)
    find_program(BXOS_NM      NAMES i686-elf-nm      x86_64-elf-nm      nm      REQUIRED)
endif()

# objcopy 의 -B i386 입력은 NASM 출력과 동일한 architecture 를 가리켜야 함.
# x86_64-elf-objcopy 도 -O elf32-i386 -B i386 을 받아준다.

# ─── C 컴파일 옵션 ───────────────────────────────────────────────────
# toolchain 파일에서 freestanding/-m32 등을 이미 잡아주지만, 커널 전용
# include path 만 여기서 추가한다.
set(BXOS_KERNEL_INCLUDE_FLAGS "-I${BXOS_KSRC} -idirafter ${BXOS_ZINC}")

# 헬퍼: 커널 *.c → *.o
function(_bxos_compile_c src obj)
    separate_arguments(_cflags NATIVE_COMMAND "${CMAKE_C_FLAGS}")
    separate_arguments(_incflags NATIVE_COMMAND "${BXOS_KERNEL_INCLUDE_FLAGS}")
    add_custom_command(
        OUTPUT  "${obj}"
        COMMAND ${CMAKE_C_COMPILER} ${_cflags}
                -O2
                ${_incflags}
                -c -o "${obj}" "${src}"
        DEPENDS "${src}"
        COMMENT "[cc] ${src}"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )
endfunction()

# 헬퍼: *.nas → patched .nasm.nas → object/bin
#   format: bin   → flat binary, 출력 = .bin
#   format: elf32 → ELF i386 object, 출력 = .o
function(_bxos_assemble_nas src out fmt)
    get_filename_component(_stem "${src}" NAME_WE)
    set(_patched "${BXOS_BUILD}/${_stem}.nasm.nas")
    add_custom_command(
        OUTPUT  "${out}" "${_patched}"
        COMMAND ${BXOS_PYTHON} "${BXOS_NAS2NASM}" "${src}" "${_patched}"
        COMMAND ${BXOS_NASM} -f ${fmt} -o "${out}" "${_patched}"
        DEPENDS "${src}" "${BXOS_NAS2NASM}"
        COMMENT "[nasm:${fmt}] ${src}"
        VERBATIM
    )
endfunction()

# ─── 1) ipl09.bin (boot sector) ──────────────────────────────────────
set(BXOS_IPL09_BIN "${BXOS_BUILD}/ipl09.bin")
_bxos_assemble_nas("${BXOS_KSRC}/ipl09.nas" "${BXOS_IPL09_BIN}" bin)

# ─── 2) asmhead.bin ──────────────────────────────────────────────────
set(BXOS_ASMHEAD_BIN "${BXOS_BUILD}/asmhead.bin")
_bxos_assemble_nas("${BXOS_KSRC}/asmhead.nas" "${BXOS_ASMHEAD_BIN}" bin)

# ─── 3) naskfunc.o ───────────────────────────────────────────────────
set(BXOS_NASKFUNC_O "${BXOS_BUILD}/naskfunc.o")
_bxos_assemble_nas("${BXOS_KSRC}/naskfunc.nas" "${BXOS_NASKFUNC_O}" elf32)

# ─── 4) 커널 C 소스 → object ─────────────────────────────────────────
set(BXOS_KERNEL_C_NAMES
    bootpack graphic dsctbl int fifo keyboard mouse memory sheet timer
    mtask window console file tek
)
set(BXOS_KERNEL_OBJS
    "${BXOS_NASKFUNC_O}"
)
foreach(_n IN LISTS BXOS_KERNEL_C_NAMES)
    set(_src "${BXOS_KSRC}/${_n}.c")
    set(_obj "${BXOS_BUILD}/${_n}.o")
    _bxos_compile_c("${_src}" "${_obj}")
    list(APPEND BXOS_KERNEL_OBJS "${_obj}")
endforeach()

# modern_libc.c
set(BXOS_MODERN_LIBC_O "${BXOS_BUILD}/modern_libc.o")
_bxos_compile_c("${BXOS_TOOLS_DIR}/modern_libc.c" "${BXOS_MODERN_LIBC_O}")
list(APPEND BXOS_KERNEL_OBJS "${BXOS_MODERN_LIBC_O}")

# ─── 5) hankaku.txt → hankaku.bin → hankaku.o ────────────────────────
set(BXOS_HANKAKU_BIN "${BXOS_BUILD}/hankaku.bin")
set(BXOS_HANKAKU_O   "${BXOS_BUILD}/hankaku.o")
add_custom_command(
    OUTPUT  "${BXOS_HANKAKU_BIN}"
    COMMAND ${BXOS_PYTHON} "${BXOS_MAKEFONT}" "${BXOS_KSRC}/hankaku.txt" "${BXOS_HANKAKU_BIN}"
    DEPENDS "${BXOS_KSRC}/hankaku.txt" "${BXOS_MAKEFONT}"
    COMMENT "[makefont] hankaku.bin"
    VERBATIM
)
# objcopy 를 build dir 에서 실행해야 _binary_hankaku_bin_* 심볼 이름이 맞음.
add_custom_command(
    OUTPUT  "${BXOS_HANKAKU_O}"
    WORKING_DIRECTORY "${BXOS_BUILD}"
    COMMAND ${BXOS_OBJCOPY}
            -I binary -O elf32-i386 -B i386
            --rename-section .data=.rodata
            --redefine-sym _binary_hankaku_bin_start=_hankaku
            --redefine-sym _binary_hankaku_bin_end=_ehankaku
            --redefine-sym _binary_hankaku_bin_size=_hankaku_size
            hankaku.bin hankaku.o
    DEPENDS "${BXOS_HANKAKU_BIN}"
    COMMENT "[objcopy] hankaku.bin → hankaku.o"
    VERBATIM
)
list(APPEND BXOS_KERNEL_OBJS "${BXOS_HANKAKU_O}")

# ─── 6) startup_kernel.s → ELF object ────────────────────────────────
set(BXOS_STARTUP_O "${BXOS_BUILD}/startup_kernel.o")
add_custom_command(
    OUTPUT  "${BXOS_STARTUP_O}"
    COMMAND ${BXOS_NASM} -f elf32 -o "${BXOS_STARTUP_O}"
            "${BXOS_TOOLS_DIR}/startup_kernel.s"
    DEPENDS "${BXOS_TOOLS_DIR}/startup_kernel.s"
    COMMENT "[nasm:elf32] startup_kernel.s"
    VERBATIM
)
list(APPEND BXOS_KERNEL_OBJS "${BXOS_STARTUP_O}")

# ─── 7) link → bootpack.elf ──────────────────────────────────────────
set(BXOS_BOOTPACK_ELF "${BXOS_BUILD}/bootpack.elf")
add_custom_command(
    OUTPUT  "${BXOS_BOOTPACK_ELF}"
    COMMAND ${BXOS_LD}
            -m elf_i386 -nostdlib -static
            -T "${BXOS_LDS}"
            -o "${BXOS_BOOTPACK_ELF}"
            ${BXOS_KERNEL_OBJS}
    DEPENDS ${BXOS_KERNEL_OBJS} "${BXOS_LDS}"
    COMMENT "[ld] bootpack.elf"
    VERBATIM
    COMMAND_EXPAND_LISTS
)

# ─── 8) bootpack.bin (flat binary) ───────────────────────────────────
set(BXOS_BOOTPACK_BIN "${BXOS_BUILD}/bootpack.bin")
add_custom_command(
    OUTPUT  "${BXOS_BOOTPACK_BIN}"
    COMMAND ${BXOS_OBJCOPY} -O binary "${BXOS_BOOTPACK_ELF}" "${BXOS_BOOTPACK_BIN}"
    DEPENDS "${BXOS_BOOTPACK_ELF}"
    COMMENT "[objcopy] bootpack.elf → bootpack.bin"
    VERBATIM
)

# ─── 9) bootpack.bin → bootpack.hrb (HRB header by hrbify.py) ────────
# Makefile.modern 처럼 nm 출력에서 __hrb_data_* 심볼을 뽑아 헤더에 넣는다.
# 멀티라인 셸 substitution 을 Makefile 에 안전하게 표현하기 위해
# 헬퍼 스크립트(cmake/finalize-bootpack-hrb.sh) 한 줄 호출로 처리한다.
set(BXOS_BOOTPACK_HRB    "${BXOS_BUILD}/bootpack.hrb")
set(BXOS_FINALIZE_BOOTPACK "${CMAKE_SOURCE_DIR}/cmake/finalize-bootpack-hrb.sh")
add_custom_command(
    OUTPUT  "${BXOS_BOOTPACK_HRB}"
    COMMAND ${BXOS_FINALIZE_BOOTPACK}
            "${BXOS_NM}" "${BXOS_PYTHON}" "${BXOS_HRBIFY}"
            "${BXOS_BOOTPACK_ELF}" "${BXOS_BOOTPACK_BIN}" "${BXOS_BOOTPACK_HRB}"
    DEPENDS "${BXOS_BOOTPACK_BIN}" "${BXOS_BOOTPACK_ELF}"
            "${BXOS_HRBIFY}" "${BXOS_FINALIZE_BOOTPACK}"
    COMMENT "[hrbify] bootpack.bin → bootpack.hrb"
    VERBATIM
)

# ─── 10) haribote.sys = asmhead.bin + bootpack.hrb ───────────────────
set(BXOS_HARIBOTE_SYS "${BXOS_BUILD}/haribote.sys")
add_custom_command(
    OUTPUT  "${BXOS_HARIBOTE_SYS}"
    COMMAND ${CMAKE_COMMAND} -E cat "${BXOS_ASMHEAD_BIN}" "${BXOS_BOOTPACK_HRB}"
            > "${BXOS_HARIBOTE_SYS}"
    DEPENDS "${BXOS_ASMHEAD_BIN}" "${BXOS_BOOTPACK_HRB}"
    COMMENT "[cat] asmhead.bin + bootpack.hrb → haribote.sys"
    VERBATIM
)

# ─── 11) 앱 등록 (HariboteApp.cmake) ─────────────────────────────────
include(cmake/HariboteApp.cmake)

# 기본: harib27f 의 모든 prebuilt .hrb 를 IMG 에 포함.
# (build-modern.sh 가 wildcard 로 하던 것과 동일.)
set(BXOS_APPS_PREBUILT
    a hello3 hello4 hello5 winhelo winhelo2 winhelo3
    star1 stars stars2 lines walk noodle beepdown
    color color2 sosu sosu2 sosu3 type iroha chklang
    notrec bball invader calc tview mmlplay gview
)
foreach(_app IN LISTS BXOS_APPS_PREBUILT)
    bxos_register_prebuilt_app(${_app})
endforeach()

# (참고) 앱을 CMake 로 직접 빌드하고 싶을 때 — 현재는 미완성이지만 hook 은 준비됨:
#   bxos_build_apilib()
#   bxos_add_haribote_app(NAME a SOURCES a.c STACK 1k MALLOC 0k)
#   bxos_register_built_app(a)

# ─── 12) haribote.img (FAT12 disk image) ─────────────────────────────
get_property(BXOS_IMG_EXTRA_FILES GLOBAL PROPERTY BXOS_IMG_EXTRA_FILES)

set(BXOS_HARIBOTE_IMG "${BXOS_BUILD}/haribote.img")
set(BXOS_IMG_FILES
    "${BXOS_HARIBOTE_SYS}"
    "${BXOS_KSRC}/ipl09.nas"
    "${BXOS_HARIB}/make.bat"
    ${BXOS_IMG_EXTRA_FILES}
    "${BXOS_HARIB}/euc.txt"
    "${BXOS_HARIB}/mmldata/kirakira.mml"
    "${BXOS_HARIB}/mmldata/daigo.mml"
    "${BXOS_HARIB}/mmldata/daiku.mml"
    "${BXOS_HARIB}/pictdata/fujisan.jpg"
    "${BXOS_HARIB}/pictdata/night.bmp"
    "${BXOS_HARIB}/nihongo/nihongo.fnt"
)

add_custom_command(
    OUTPUT  "${BXOS_HARIBOTE_IMG}"
    COMMAND ${BXOS_PYTHON} "${BXOS_MKFAT12}"
            --boot "${BXOS_IPL09_BIN}"
            --out  "${BXOS_HARIBOTE_IMG}"
            ${BXOS_IMG_FILES}
    DEPENDS "${BXOS_IPL09_BIN}" "${BXOS_HARIBOTE_SYS}" ${BXOS_IMG_FILES}
    COMMENT "[mkfat12] haribote.img"
    VERBATIM
    COMMAND_EXPAND_LISTS
)

# ─── 13) phony 타겟들 ────────────────────────────────────────────────
add_custom_target(kernel  DEPENDS "${BXOS_BOOTPACK_HRB}")
add_custom_target(sys     DEPENDS "${BXOS_HARIBOTE_SYS}")
add_custom_target(image   DEPENDS "${BXOS_HARIBOTE_IMG}")
add_custom_target(bxos ALL DEPENDS "${BXOS_HARIBOTE_IMG}")

# QEMU 부팅 (편의용)
find_program(BXOS_QEMU NAMES qemu-system-i386)
if(BXOS_QEMU)
    add_custom_target(run
        COMMAND ${BXOS_QEMU} -m 32 -fda "${BXOS_HARIBOTE_IMG}" -boot a
        DEPENDS "${BXOS_HARIBOTE_IMG}"
        USES_TERMINAL
        COMMENT "qemu-system-i386 -fda haribote.img"
    )
endif()

# ─── 14) info ────────────────────────────────────────────────────────
# 리스트 변수는 ; 로 join 해서 한 인자로 묶어야 셸에 단어로 쪼개지지 않음.
string(REPLACE ";" " " _BXOS_KC_PRINT  "${BXOS_KERNEL_C_NAMES}")
string(REPLACE ";" " " _BXOS_APP_PRINT "${BXOS_APPS_PREBUILT}")
add_custom_target(info
    COMMAND ${CMAKE_COMMAND} -E echo "PROJECT_DIR = ${CMAKE_SOURCE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "BUILD       = ${BXOS_BUILD}"
    COMMAND ${CMAKE_COMMAND} -E echo "CC          = ${CMAKE_C_COMPILER}"
    COMMAND ${CMAKE_COMMAND} -E echo "LD          = ${BXOS_LD}"
    COMMAND ${CMAKE_COMMAND} -E echo "OBJCOPY     = ${BXOS_OBJCOPY}"
    COMMAND ${CMAKE_COMMAND} -E echo "NM          = ${BXOS_NM}"
    COMMAND ${CMAKE_COMMAND} -E echo "NASM        = ${BXOS_NASM}"
    COMMAND ${CMAKE_COMMAND} -E echo "PYTHON      = ${BXOS_PYTHON}"
    COMMAND ${CMAKE_COMMAND} -E echo "C_FLAGS     = ${CMAKE_C_FLAGS}"
    COMMAND ${CMAKE_COMMAND} -E echo "Kernel C    = ${_BXOS_KC_PRINT}"
    COMMAND ${CMAKE_COMMAND} -E echo "Apps        = ${_BXOS_APP_PRINT}"
)

message(STATUS "BxOS configured:")
message(STATUS "  CC      = ${CMAKE_C_COMPILER}")
message(STATUS "  LD      = ${BXOS_LD}")
message(STATUS "  NASM    = ${BXOS_NASM}")
message(STATUS "  PYTHON  = ${BXOS_PYTHON}")
message(STATUS "  Apps    = ${BXOS_APPS_PREBUILT}")

if(CMAKE_C_COMPILER MATCHES "/usr/bin/cc" OR CMAKE_C_COMPILER MATCHES "/usr/bin/gcc")
    if(APPLE)
        message(WARNING
            "현재 컴파일러가 시스템 기본값(${CMAKE_C_COMPILER})으로 설정되었습니다.\n"
            "이 상태로는 i386(32bit) 빌드가 실패할 수 있습니다. (특히 Apple Silicon)\n"
            "CLion 설정에서 'Clear Cache and Reload'를 수행하거나, "
            "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-i686-elf.cmake 옵션을 확인해 주세요."
        )
    endif()
endif()
