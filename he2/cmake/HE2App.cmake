# HE2App.cmake — CMake helpers for building BxOS HE2 user-space apps.
#
# 제공 매크로:
#   bxos_libbxos()                                    # libbxos.a 빌드 타겟 등록
#   he2_add_app(NAME [SOURCES ...] [STACK N] [HEAP N])
#       # .he2 실행 파일 생성. 결과: ${BXOS_HE2_BIN_DIR}/<NAME>.he2
#
# 주의:
#   - HE2 앱은 root 의 BxOS 커널 toolchain 과 같은 i686-elf-gcc 를 쓰지만,
#     `-fleading-underscore` 같은 nask 호환 플래그는 *쓰지 않는다*.
#     HE2 앱의 INT 0x40 syscall 은 GCC inline asm 으로 직접 발행하기 때문에
#     symbol naming convention 이 무관하다.
#   - 모든 .c → .o 는 add_custom_command 로 직접 호출. 일반 CMake C 타겟은
#     상속된 toolchain CFLAGS (위의 -fleading-underscore 등) 가 끼어들 수
#     있어 피한다.

if(NOT DEFINED BXOS_HE2_DIR)
    message(FATAL_ERROR "BXOS_HE2_DIR not set before include(HE2App.cmake)")
endif()

if(NOT DEFINED BXOS_CC OR NOT DEFINED BXOS_LD)
    message(FATAL_ERROR "BXOS_CC / BXOS_LD must be set (toolchain file).")
endif()

set(BXOS_HE2_TOOLS    "${BXOS_HE2_DIR}/tools")
set(BXOS_HE2_INCLUDE  "${BXOS_HE2_DIR}/libbxos/include")
set(BXOS_HE2_LIBSRC   "${BXOS_HE2_DIR}/libbxos/src")
set(BXOS_HE2_LDS      "${BXOS_HE2_TOOLS}/linker-he2.lds")
set(BXOS_HE2_PACK     "${BXOS_HE2_TOOLS}/he2pack.py")
set(BXOS_HE2_BUILD    "${CMAKE_BINARY_DIR}/he2")
set(BXOS_HE2_BIN_DIR  "${BXOS_HE2_BUILD}/bin")
file(MAKE_DIRECTORY "${BXOS_HE2_BIN_DIR}")

find_program(BXOS_HE2_AR NAMES i686-elf-ar x86_64-elf-ar ar REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# 32-bit user-mode app 컴파일에 필요한 최소 플래그.
# 일부 i686-elf-gcc 는 -m32 가 기본이지만 명시적으로 잡아준다.
set(BXOS_HE2_CFLAGS
    -m32 -O2 -ffreestanding -fno-pic -fno-pie -fno-pic
    -fno-stack-protector -fno-asynchronous-unwind-tables
    -fno-builtin -nostdinc -nostdlib -fno-common
    -Wall -Wno-implicit-function-declaration
    -Wno-int-conversion -Wno-pointer-sign
    -Wno-incompatible-pointer-types
    -I${BXOS_HE2_INCLUDE}
)

# 어셈블리 (.S) 도 같은 -m32 + -nostdinc 로 처리.
set(BXOS_HE2_ASMFLAGS
    -m32 -nostdinc -I${BXOS_HE2_INCLUDE}
)

# ─── libbxos.a 빌드 ────────────────────────────────────────────────────
function(bxos_libbxos)
    set(_objs)
    foreach(src crt0.S syscall.c)
        get_filename_component(_stem "${src}" NAME_WE)
        set(_obj "${BXOS_HE2_BUILD}/libbxos_${_stem}.o")
        if(src MATCHES "\\.S$")
            add_custom_command(
                OUTPUT "${_obj}"
                COMMAND ${BXOS_CC} ${BXOS_HE2_ASMFLAGS} -c
                        -o "${_obj}" "${BXOS_HE2_LIBSRC}/${src}"
                DEPENDS "${BXOS_HE2_LIBSRC}/${src}"
                COMMENT "[he2/libbxos] AS ${src}"
                VERBATIM
            )
        else()
            add_custom_command(
                OUTPUT "${_obj}"
                COMMAND ${BXOS_CC} ${BXOS_HE2_CFLAGS} -c
                        -o "${_obj}" "${BXOS_HE2_LIBSRC}/${src}"
                DEPENDS "${BXOS_HE2_LIBSRC}/${src}"
                        "${BXOS_HE2_INCLUDE}/bxos.h"
                        "${BXOS_HE2_INCLUDE}/he2.h"
                COMMENT "[he2/libbxos] CC ${src}"
                VERBATIM
            )
        endif()
        list(APPEND _objs "${_obj}")
    endforeach()

    set(_lib "${BXOS_HE2_BUILD}/libbxos.a")
    add_custom_command(
        OUTPUT "${_lib}"
        COMMAND ${CMAKE_COMMAND} -E remove -f "${_lib}"
        COMMAND ${BXOS_HE2_AR} rcs "${_lib}" ${_objs}
        DEPENDS ${_objs}
        COMMENT "[he2/libbxos] AR libbxos.a"
        VERBATIM
    )
    add_custom_target(bxos_libbxos_target DEPENDS "${_lib}")
    set(BXOS_HE2_LIB "${_lib}" CACHE INTERNAL "libbxos.a path")
endfunction()


# ─── 단일 HE2 앱 빌드 ──────────────────────────────────────────────────
#
#   he2_add_app(name
#       [SOURCES src1.c src2.c ...]    # 생략하면 ${name}.c 한 개로 가정
#       [STACK   N]                    # 기본 16384
#       [HEAP    N])                   # 기본 1048576
#
#   결과:   ${BXOS_HE2_BIN_DIR}/<name>.he2
#   전체 ALL 빌드에 등록되며, get target name = "he2_app_<name>".
#
function(he2_add_app NAME)
    set(_oneval STACK HEAP DIR)
    set(_multival SOURCES)
    cmake_parse_arguments(HEA "" "${_oneval}" "${_multival}" ${ARGN})

    if(NOT HEA_DIR)
        set(HEA_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    if(NOT HEA_SOURCES)
        set(HEA_SOURCES "${HEA_DIR}/${NAME}.c")
    else()
        set(_resolved)
        foreach(_s IN LISTS HEA_SOURCES)
            if(IS_ABSOLUTE "${_s}")
                list(APPEND _resolved "${_s}")
            else()
                list(APPEND _resolved "${HEA_DIR}/${_s}")
            endif()
        endforeach()
        set(HEA_SOURCES "${_resolved}")
    endif()

    if(NOT HEA_STACK)
        set(HEA_STACK 16384)
    endif()
    if(NOT HEA_HEAP)
        set(HEA_HEAP 1048576)
    endif()

    if(NOT BXOS_HE2_LIB)
        message(FATAL_ERROR "Call bxos_libbxos() before he2_add_app(${NAME}).")
    endif()

    set(_objdir "${BXOS_HE2_BUILD}/obj/${NAME}")
    file(MAKE_DIRECTORY "${_objdir}")

    set(_objs)
    foreach(_src IN LISTS HEA_SOURCES)
        get_filename_component(_stem "${_src}" NAME_WE)
        set(_obj "${_objdir}/${_stem}.o")
        add_custom_command(
            OUTPUT "${_obj}"
            COMMAND ${BXOS_CC} ${BXOS_HE2_CFLAGS} -c -o "${_obj}" "${_src}"
            DEPENDS "${_src}"
                    "${BXOS_HE2_INCLUDE}/bxos.h"
                    "${BXOS_HE2_INCLUDE}/apilib.h"
            COMMENT "[he2/${NAME}] CC ${_src}"
            VERBATIM
        )
        list(APPEND _objs "${_obj}")
    endforeach()

    set(_elf "${_objdir}/${NAME}.elf")
    add_custom_command(
        OUTPUT "${_elf}"
        COMMAND ${BXOS_LD}
                -m elf_i386
                -T "${BXOS_HE2_LDS}"
                --defsym=_bxos_stack_size=${HEA_STACK}
                --defsym=_bxos_heap_size=${HEA_HEAP}
                -o "${_elf}"
                ${_objs}
                "${BXOS_HE2_LIB}"
        DEPENDS ${_objs} "${BXOS_HE2_LIB}" "${BXOS_HE2_LDS}"
        COMMENT "[he2/${NAME}] LD ${NAME}.elf  stack=${HEA_STACK} heap=${HEA_HEAP}"
        VERBATIM
    )

    set(_he2 "${BXOS_HE2_BIN_DIR}/${NAME}.he2")
    add_custom_command(
        OUTPUT "${_he2}"
        COMMAND ${Python3_EXECUTABLE} "${BXOS_HE2_PACK}"
                --in "${_elf}" --out "${_he2}"
                --objcopy "${BXOS_OBJCOPY}"
        DEPENDS "${_elf}" "${BXOS_HE2_PACK}"
        COMMENT "[he2/${NAME}] PACK ${NAME}.he2"
        VERBATIM
    )

    add_custom_target("he2_app_${NAME}" ALL DEPENDS "${_he2}")
    set_property(GLOBAL APPEND PROPERTY BXOS_HE2_APPS "${_he2}")
endfunction()
