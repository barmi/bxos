# cmake/HariboteApp.cmake
#
# BxOS 앱(.hrb) 빌드 hook.
#
# 현재 상태(2026-04 기준):
#   - 커널은 CMake 로 처음부터 다시 빌드한다.
#   - 앱(.hrb)은 원본 빌드본을 재사용하는 것이 기본 동작이다.
#     (harib27f/<app>/<app>.hrb 가 이미 존재하므로 디스크 이미지에 그대로 복사)
#
# 이 모듈이 제공하는 것:
#   1) bxos_register_prebuilt_app(<dir>) — 기존 .hrb 를 그대로 IMG 에 포함.
#   2) bxos_build_apilib()              — apilib/api*.nas 를 patch + nasm 으로 모아
#                                          static archive(libapilib.a) 생성. (실험적)
#   3) bxos_add_haribote_app(NAME ... SOURCES ... STACK ... MALLOC ...)
#                                        — .c/.nas 를 .hrb 로 빌드. (실험적, 미완성)
#
# 미완성 부분 (의도적):
#   - 앱 .hrb 는 OSASKCMP 압축 + 헤더 chain 이라 hrbify.py 만으로는 100%
#     원본과 동일한 결과를 만들지 못한다. 비압축 모드 hook 만 제공한다.
#   - bim2bin (-osacmp) 대체가 필요하다. 향후 hrbify.py 를 확장하거나
#     별도 압축기를 추가해야 한다.
#
# 그래서 기본 동작은 (1) 이고, (2)/(3) 은 향후 확장용 골격이다.

include_guard(GLOBAL)

# IMG 에 들어갈 추가 파일을 모으는 전역 리스트.
set_property(GLOBAL PROPERTY BXOS_IMG_EXTRA_FILES "")

function(_bxos_img_extra_append path)
    get_property(_cur GLOBAL PROPERTY BXOS_IMG_EXTRA_FILES)
    list(APPEND _cur "${path}")
    set_property(GLOBAL PROPERTY BXOS_IMG_EXTRA_FILES "${_cur}")
endfunction()

# ─────────────────────────────────────────────────────────────────────
# (1) 기존 .hrb 를 그대로 디스크 이미지에 포함
#
# 사용:
#   bxos_register_prebuilt_app(a)
#   bxos_register_prebuilt_app(bball)
#
# harib27f/<dir>/<dir>.hrb 가 존재해야 한다.
# ─────────────────────────────────────────────────────────────────────
function(bxos_register_prebuilt_app dir)
    set(_hrb "${CMAKE_SOURCE_DIR}/harib27f/${dir}/${dir}.hrb")
    if(NOT EXISTS "${_hrb}")
        message(WARNING "[bxos] prebuilt app .hrb not found: ${_hrb}")
        return()
    endif()
    _bxos_img_extra_append("${_hrb}")
endfunction()

# ─────────────────────────────────────────────────────────────────────
# (2) apilib NASM 모음을 정적 라이브러리로 빌드 (실험적)
#
# 원본은 nask + golib00.exe 로 *.lib 를 만든다. 여기서는 NASM 으로 ELF
# 오브젝트를 만들고 ar 로 묶어 libapilib.a 를 만든다. 앱 링크에서 -lapilib
# 로 사용 가능.
# ─────────────────────────────────────────────────────────────────────
function(bxos_build_apilib)
    set(_apidir "${CMAKE_SOURCE_DIR}/harib27f/apilib")
    if(NOT IS_DIRECTORY "${_apidir}")
        message(WARNING "[bxos] apilib directory not found: ${_apidir}")
        return()
    endif()
    file(GLOB _api_nas RELATIVE "${_apidir}" "${_apidir}/api*.nas" "${_apidir}/alloca.nas")
    if(NOT _api_nas)
        message(WARNING "[bxos] no api*.nas under ${_apidir}")
        return()
    endif()

    set(_outdir "${CMAKE_BINARY_DIR}/apilib")
    file(MAKE_DIRECTORY "${_outdir}")

    set(_objs)
    foreach(_n IN LISTS _api_nas)
        get_filename_component(_stem "${_n}" NAME_WE)
        set(_src    "${_apidir}/${_n}")
        set(_patched "${_outdir}/${_stem}.nasm.nas")
        set(_obj    "${_outdir}/${_stem}.o")
        add_custom_command(
            OUTPUT  "${_obj}"
            COMMAND ${BXOS_PYTHON} "${BXOS_NAS2NASM}" "${_src}" "${_patched}"
            COMMAND ${BXOS_NASM}   -f elf32 -o "${_obj}" "${_patched}"
            DEPENDS "${_src}" "${BXOS_NAS2NASM}"
            COMMENT "[apilib] ${_stem}.nas → ${_stem}.o"
            VERBATIM
        )
        list(APPEND _objs "${_obj}")
    endforeach()

    # ar 로 정적 아카이브 생성.
    set(_lib "${_outdir}/libapilib.a")
    add_custom_command(
        OUTPUT  "${_lib}"
        COMMAND ${CMAKE_AR} rcs "${_lib}" ${_objs}
        DEPENDS ${_objs}
        COMMENT "[apilib] libapilib.a"
        VERBATIM
    )
    add_custom_target(apilib DEPENDS "${_lib}")
    set_property(GLOBAL PROPERTY BXOS_APILIB_PATH "${_lib}")
endfunction()

# ─────────────────────────────────────────────────────────────────────
# (3) 앱 .hrb 빌드 hook (실험적, 골격만 제공)
#
# 사용 예 (현재는 동작 보장 X — bim2bin -osacmp 대체가 없어 압축 .hrb 와는
# 바이트 단위로 다를 수 있다):
#
#   bxos_add_haribote_app(NAME a
#                         SOURCES a.c
#                         STACK 1k MALLOC 0k)
#
# 동작:
#   - SOURCES 의 .c 는 i686-elf-gcc 로 컴파일
#   - SOURCES 의 .nas 는 nas2nasm + NASM 으로 컴파일
#   - apilib (bxos_build_apilib() 가 먼저 호출되어 있어야 함) 와 함께 링크
#   - hrbify.py 로 .hrb 헤더 부착 (비압축)
# ─────────────────────────────────────────────────────────────────────
function(bxos_add_haribote_app)
    cmake_parse_arguments(APP "" "NAME;STACK;MALLOC;DIR" "SOURCES" ${ARGN})
    if(NOT APP_NAME)
        message(FATAL_ERROR "bxos_add_haribote_app: NAME required")
    endif()
    if(NOT APP_DIR)
        set(APP_DIR "${CMAKE_SOURCE_DIR}/harib27f/${APP_NAME}")
    endif()
    if(NOT APP_STACK)
        set(APP_STACK "1k")
    endif()
    if(NOT APP_MALLOC)
        set(APP_MALLOC "0k")
    endif()

    get_property(_apilib GLOBAL PROPERTY BXOS_APILIB_PATH)
    if(NOT _apilib)
        message(WARNING
            "[bxos] bxos_add_haribote_app(${APP_NAME}) 호출 전에 "
            "bxos_build_apilib() 를 호출해야 합니다. 스킵.")
        return()
    endif()

    set(_outdir "${CMAKE_BINARY_DIR}/apps/${APP_NAME}")
    file(MAKE_DIRECTORY "${_outdir}")

    # SOURCES → object 파일 리스트
    set(_objs)
    foreach(_src IN LISTS APP_SOURCES)
        if(NOT IS_ABSOLUTE "${_src}")
            set(_src "${APP_DIR}/${_src}")
        endif()
        get_filename_component(_stem "${_src}" NAME_WE)
        get_filename_component(_ext  "${_src}" EXT)

        if(_ext STREQUAL ".c")
            set(_obj "${_outdir}/${_stem}.o")
            add_custom_command(
                OUTPUT  "${_obj}"
                COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_FLAGS}
                        -I${CMAKE_SOURCE_DIR}/harib27f
                        -c -o "${_obj}" "${_src}"
                DEPENDS "${_src}"
                COMMENT "[app:${APP_NAME}] cc ${_stem}.c"
                VERBATIM
            )
        elseif(_ext STREQUAL ".nas")
            set(_patched "${_outdir}/${_stem}.nasm.nas")
            set(_obj "${_outdir}/${_stem}.o")
            add_custom_command(
                OUTPUT  "${_obj}"
                COMMAND ${BXOS_PYTHON} "${BXOS_NAS2NASM}" "${_src}" "${_patched}"
                COMMAND ${BXOS_NASM}   -f elf32 -o "${_obj}" "${_patched}"
                DEPENDS "${_src}" "${BXOS_NAS2NASM}"
                COMMENT "[app:${APP_NAME}] nasm ${_stem}.nas"
                VERBATIM
            )
        else()
            message(WARNING "[bxos] unknown source ext '${_ext}' for ${_src}")
            continue()
        endif()
        list(APPEND _objs "${_obj}")
    endforeach()

    # 링크 → flat → hrbify (비압축)
    set(_elf "${_outdir}/${APP_NAME}.elf")
    set(_bin "${_outdir}/${APP_NAME}.bin")
    set(_hrb "${_outdir}/${APP_NAME}.hrb")

    # 앱용 링커 스크립트는 별도. 우선 -Ttext 0 정도로 단순 처리.
    add_custom_command(
        OUTPUT  "${_elf}"
        COMMAND ${BXOS_LD} -m elf_i386 -nostdlib -static -e _HariMain -Ttext 0
                -o "${_elf}" ${_objs} "${_apilib}"
        DEPENDS ${_objs} "${_apilib}"
        COMMENT "[app:${APP_NAME}] ld → ${APP_NAME}.elf"
        VERBATIM
    )
    add_custom_command(
        OUTPUT  "${_bin}"
        COMMAND ${BXOS_OBJCOPY} -O binary "${_elf}" "${_bin}"
        DEPENDS "${_elf}"
        COMMENT "[app:${APP_NAME}] objcopy → ${APP_NAME}.bin"
        VERBATIM
    )
    add_custom_command(
        OUTPUT  "${_hrb}"
        COMMAND ${BXOS_PYTHON} "${BXOS_HRBIFY}" --in "${_bin}" --out "${_hrb}"
                --stack-top 0 --esp-init 0 --malloc ${APP_MALLOC}
                --data-size 0 --data-file 0
        DEPENDS "${_bin}" "${BXOS_HRBIFY}"
        COMMENT "[app:${APP_NAME}] hrbify (uncompressed) → ${APP_NAME}.hrb"
        VERBATIM
    )

    add_custom_target(app_${APP_NAME} DEPENDS "${_hrb}")
    set_property(GLOBAL PROPERTY BXOS_APP_${APP_NAME}_HRB "${_hrb}")

    # 새로 빌드한 .hrb 를 IMG 에 포함시키려면 사용자가 명시적으로
    # bxos_register_built_app(${APP_NAME}) 을 호출하도록 둔다.
endfunction()

# 새로 빌드한 .hrb 를 IMG 에 포함 (실험적)
function(bxos_register_built_app name)
    get_property(_hrb GLOBAL PROPERTY BXOS_APP_${name}_HRB)
    if(NOT _hrb)
        message(WARNING "[bxos] no built app named '${name}'")
        return()
    endif()
    _bxos_img_extra_append("${_hrb}")
endfunction()
