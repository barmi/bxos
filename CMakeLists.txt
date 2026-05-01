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
# 동작:
#   - 커널(haribote.sys) 은 NASM + i686-elf-gcc + Python 으로 새로 빌드.
#   - 앱은 HE2(.he2) 포맷만 IMG 에 포함한다. 기존 .hrb 는 더 이상 포함하지 않는다.

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
set(BXOS_FAT_TOOL "${BXOS_TOOLS_DIR}/bxos_fat.py")
set(BXOS_LDS      "${BXOS_TOOLS_DIR}/linker-bootpack.lds")
set(BXOS_KERNEL_HEADERS
    "${BXOS_KSRC}/bootpack.h"
    "${BXOS_TOOLS_DIR}/euckr_map.h"
)

foreach(_t IN ITEMS
        "${BXOS_NAS2NASM}" "${BXOS_HRBIFY}" "${BXOS_MAKEFONT}"
        "${BXOS_MKFAT12}" "${BXOS_FAT_TOOL}" "${BXOS_LDS}")
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
#
# CMAKE_BUILD_TYPE = Debug 일 때는 -O0 -g (DWARF) 로 빌드해 GDB / CLion remote
# debugger 에서 source-level breakpoint 가 가능. 그 외는 -O2 (release).
function(_bxos_compile_c src obj)
    separate_arguments(_cflags NATIVE_COMMAND "${CMAKE_C_FLAGS}")
    separate_arguments(_incflags NATIVE_COMMAND "${BXOS_KERNEL_INCLUDE_FLAGS}")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_optflags "-O0" "-g" "-gdwarf-4")
    else()
        set(_optflags "-O2")
    endif()
    add_custom_command(
        OUTPUT  "${obj}"
        COMMAND ${CMAKE_C_COMPILER} ${_cflags}
                ${_optflags}
                ${_incflags}
                -c -o "${obj}" "${src}"
        DEPENDS "${src}" ${BXOS_KERNEL_HEADERS}
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
    mtask window menu console file tek ata fs_fat
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
# 이전에는 harib27f/<app>/<app>.hrb 를 그대로 IMG 에 포함했지만,
# 이제는 HE2 빌드(아래 11b)만 사용한다. HariboteApp.cmake 의
# bxos_register_prebuilt_app / bxos_add_haribote_app 헬퍼는 호환성을 위해
# 그대로 두지만, 기본 빌드에서는 호출하지 않는다.
include(cmake/HariboteApp.cmake)

# ─── 11b) HE2 (Haribote Executable v2) 앱 빌드 ──────────────────────
#
# 새 .he2 포맷: 32B 헤더 + flat 코드/데이터, INT 0x40 syscall은
# C-inline-asm 으로 래핑된 libbxos 로 호출. 자세한 사양은
# he2/docs/HE2-FORMAT.md 참고.
#
# 같은 앱 소스(harib27f/<name>/<name>.c)를 그대로 컴파일해서 .he2 로
# 다시 빌드한다. 결과는 IMG 에 추가되어 콘솔에서 실행 가능.

set(BXOS_HE2_DIR "${CMAKE_SOURCE_DIR}/he2")
include("${BXOS_HE2_DIR}/cmake/HE2App.cmake")

bxos_libbxos()

# 마이그레이션 대상 앱들 — 일단 출력/창/입력 기본만.
set(BXOS_HE2_APPS_BASIC
    a hello3 hello4 hello5 khello
    sosu sosu2 sosu3
    type touch echo fdel pwd beepdown
    chklang
    lsdir
)
foreach(_a IN LISTS BXOS_HE2_APPS_BASIC)
    set(_src "${BXOS_HARIB}/${_a}/${_a}.c")
    if(EXISTS "${_src}")
        he2_add_app(${_a} DIR "${BXOS_HARIB}/${_a}")
        set("BXOS_HE2_OUT_${_a}" "${BXOS_HE2_BIN_DIR}/${_a}.he2")
    endif()
endforeach()

set(BXOS_HE2_APPS_WINDOW
    winhelo winhelo2 winhelo3
    star1 stars stars2 lines walk
    color color2 noodle
    tetris
    evtest
    explorer
    settings
)
foreach(_a IN LISTS BXOS_HE2_APPS_WINDOW)
    set(_src "${BXOS_HARIB}/${_a}/${_a}.c")
    if(EXISTS "${_src}")
        he2_add_app(${_a} DIR "${BXOS_HARIB}/${_a}" SUBSYSTEM WINDOW)
        set("BXOS_HE2_OUT_${_a}" "${BXOS_HE2_BIN_DIR}/${_a}.he2")
    endif()
endforeach()

# HE2 앱 산출물 — work1 Phase 1 부터는 IMG(부팅 FDD)가 아니라
# 별도 데이터 디스크(data.img)에 들어간다.
get_property(BXOS_HE2_OUTS GLOBAL PROPERTY BXOS_HE2_APPS)

# ─── 12a) haribote.img — 부팅용 FDD (FAT12 1.44MB, 최소 구성) ────────
#
# work1 Phase 1: 데이터 디스크 분리.
# - 부팅 FDD 에는 커널과 부팅 시 필요한 파일만 둔다 (폰트 파일 등).
# - 모든 .he2 앱과 데이터 파일은 아래 12b 의 data.img(HDD)로 이동.
# - 게스트가 아직 HDD 를 인식하지 못하므로 Phase 1 시점에는 앱이 실행되지 않음.
#   부팅 자체만 확인하는 단계.
set(BXOS_HARIBOTE_IMG "${BXOS_BUILD}/haribote.img")
set(BXOS_KERNEL_IMG_FILES
    "${BXOS_HARIBOTE_SYS}"
    "${BXOS_HARIB}/nihongo/nihongo.fnt"
)
add_custom_command(
    OUTPUT  "${BXOS_HARIBOTE_IMG}"
    COMMAND ${BXOS_PYTHON} "${BXOS_MKFAT12}"
            --boot "${BXOS_IPL09_BIN}"
            --out  "${BXOS_HARIBOTE_IMG}"
            ${BXOS_KERNEL_IMG_FILES}
    DEPENDS "${BXOS_IPL09_BIN}" "${BXOS_HARIBOTE_SYS}" ${BXOS_KERNEL_IMG_FILES}
    COMMENT "[mkfat12] haribote.img (kernel FDD)"
    VERBATIM
    COMMAND_EXPAND_LISTS
)

# ─── 12b) data.img — 사용자 데이터 디스크 (FAT16 32MB HDD) ───────────
#
# 모든 HE2 앱 + 데모 데이터 파일이 들어감. ATA HDD 로 부착될 예정.
set(BXOS_DATA_IMG "${BXOS_BUILD}/data.img")
set(BXOS_DATA_IMG_FILES
    ${BXOS_HE2_OUTS}
    "${BXOS_KSRC}/ipl09.nas"
    "${BXOS_HARIB}/make.bat"
    "${BXOS_HARIB}/euc.txt"
    "${BXOS_HARIB}/hangul.euc"
    "${BXOS_HARIB}/hangul.utf"
    "${BXOS_HARIB}/hangul/hangul.fnt"
    "${BXOS_HARIB}/mmldata/kirakira.mml"
    "${BXOS_HARIB}/mmldata/daigo.mml"
    "${BXOS_HARIB}/mmldata/daiku.mml"
    "${BXOS_HARIB}/pictdata/fujisan.jpg"
    "${BXOS_HARIB}/pictdata/night.bmp"
)
set(BXOS_SYSTEM_MENU_CFG     "${CMAKE_SOURCE_DIR}/_doc/system/menu.cfg.default")
set(BXOS_SYSTEM_SETTINGS_CFG "${CMAKE_SOURCE_DIR}/_doc/system/settings.cfg.default")
add_custom_command(
    OUTPUT  "${BXOS_DATA_IMG}"
    COMMAND ${BXOS_PYTHON} "${BXOS_MKFAT12}"
            --fs fat16 --size 32M
            --label BXOSDATA
            --out  "${BXOS_DATA_IMG}"
            ${BXOS_DATA_IMG_FILES}
    COMMAND ${BXOS_PYTHON} "${BXOS_FAT_TOOL}" mkdir
            "${BXOS_DATA_IMG}:/SYSTEM"
    COMMAND ${BXOS_PYTHON} "${BXOS_FAT_TOOL}" cp
            "HOST:${BXOS_SYSTEM_MENU_CFG}"
            "${BXOS_DATA_IMG}:/SYSTEM/MENU.CFG"
    COMMAND ${BXOS_PYTHON} "${BXOS_FAT_TOOL}" cp
            "HOST:${BXOS_SYSTEM_SETTINGS_CFG}"
            "${BXOS_DATA_IMG}:/SYSTEM/SETTINGS.CFG"
    DEPENDS ${BXOS_HE2_OUTS} ${BXOS_DATA_IMG_FILES}
            "${BXOS_SYSTEM_MENU_CFG}" "${BXOS_SYSTEM_SETTINGS_CFG}"
            "${BXOS_FAT_TOOL}"
    COMMENT "[mkfat12] data.img (FAT16 32MB HDD)"
    VERBATIM
    COMMAND_EXPAND_LISTS
)

# ─── 13) phony 타겟들 ────────────────────────────────────────────────
set(_BXOS_HE2_APPS_ALL ${BXOS_HE2_APPS_BASIC} ${BXOS_HE2_APPS_WINDOW})
add_custom_target(kernel     DEPENDS "${BXOS_BOOTPACK_HRB}")
add_custom_target(sys        DEPENDS "${BXOS_HARIBOTE_SYS}")
add_custom_target(kernel-img DEPENDS "${BXOS_HARIBOTE_IMG}")
add_custom_target(data-img   DEPENDS "${BXOS_DATA_IMG}")
add_custom_target(image      DEPENDS "${BXOS_HARIBOTE_IMG}" "${BXOS_DATA_IMG}")
add_custom_target(bxos ALL   DEPENDS "${BXOS_HARIBOTE_IMG}" "${BXOS_DATA_IMG}")

foreach(_a IN LISTS _BXOS_HE2_APPS_ALL)
    if(DEFINED BXOS_HE2_OUT_${_a})
        add_custom_target("install-${_a}"
            COMMAND ${BXOS_PYTHON} "${BXOS_FAT_TOOL}" cp
                    "HOST:${BXOS_HE2_OUT_${_a}}"
                    "${BXOS_DATA_IMG}:/${_a}.he2"
            DEPENDS "${BXOS_HE2_OUT_${_a}}" "${BXOS_FAT_TOOL}"
            COMMENT "[bxos_fat] install ${_a}.he2 -> data.img"
            VERBATIM
        )
    endif()
endforeach()

# QEMU 부팅 (편의용) — kernel + data 두 디스크 모두 부착.
find_program(BXOS_QEMU NAMES qemu-system-i386)
if(BXOS_QEMU)
    add_custom_target(run
        COMMAND ${BXOS_QEMU} -m 32
                -fda "${BXOS_HARIBOTE_IMG}" -boot a
                -hda "${BXOS_DATA_IMG}"
                -display cocoa,zoom-to-fit=on
        DEPENDS "${BXOS_HARIBOTE_IMG}" "${BXOS_DATA_IMG}"
        USES_TERMINAL
        COMMENT "qemu-system-i386 -fda haribote.img -hda data.img"
    )
endif()

# ─── 14) info ────────────────────────────────────────────────────────
# 리스트 변수는 ; 로 join 해서 한 인자로 묶어야 셸에 단어로 쪼개지지 않음.
string(REPLACE ";" " " _BXOS_KC_PRINT      "${BXOS_KERNEL_C_NAMES}")
string(REPLACE ";" " " _BXOS_HE2_APP_PRINT "${_BXOS_HE2_APPS_ALL}")
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
    COMMAND ${CMAKE_COMMAND} -E echo "HE2 Apps    = ${_BXOS_HE2_APP_PRINT}"
)

message(STATUS "BxOS configured:")
message(STATUS "  CC       = ${CMAKE_C_COMPILER}")
message(STATUS "  LD       = ${BXOS_LD}")
message(STATUS "  NASM     = ${BXOS_NASM}")
message(STATUS "  PYTHON   = ${BXOS_PYTHON}")
message(STATUS "  HE2 Apps = ${_BXOS_HE2_APPS_ALL}")

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
