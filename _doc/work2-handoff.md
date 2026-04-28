# work2 인수인계 — 다음 세션용 컨텍스트

이 문서는 [_doc/work2.md](work2.md) 작업을 **새 세션에서 처음 보는 사람(또는 새 Claude)** 이 끊김 없이 이어받기 위한 단일 진입점이다. 먼저 work2.md 본문을 읽고, 그다음 이 문서로 현재 위치와 다음 행동을 파악하면 된다.

work1 (쓰기 가능 FS + OS/앱 분리 빌드) 작업의 후속이다. work1 의 산출물은 [_doc/work1.md](work1.md), [_doc/work1-handoff.md](work1-handoff.md), [_doc/storage.md](storage.md) 에 정리되어 있다.

---

## 1. 한 줄 요약

work1 으로 도입된 쓰기 가능한 FAT16 데이터 디스크에 **서브디렉터리 / 다단계 경로 / cwd** 를 도입해, 콘솔·사용자 API·호스트 도구 모두 `mkdir /sub`, `cd /sub`, `cp /a /sub/b` 같은 표현을 받게 만드는 것이 목표.

## 2. 현재 위치 (2026-04-28 기준)

- **Phase 0 (계획 수립) 완료**, 본 문서 + [work2.md](work2.md) 작성됨.
- **Phase 1 (디렉터리 추상화) 코드 작업 완료**:
  - [bootpack.h](../harib27f/haribote/bootpack.h)에 `struct DIR_SLOT`, `struct DIR_ITER`, `dir_*` 프로토타입 추가.
  - [fs_fat.c](../harib27f/haribote/fs_fat.c)에 `dir_iter_open`/`dir_iter_next`/`dir_iter_close`, `dir_find`, `dir_alloc_slot`, `dir_write_slot` 구현.
  - 기존 `fs_data_search`, `fs_data_create`, `fs_data_write`, `fs_data_truncate`, `fs_data_unlink`는 root 전용 직접 슬롯 갱신 대신 directory slot 추상화를 경유.
  - 검증: `cmake --build build/cmake --target kernel`, `cmake --build build/cmake`, `fsck_msdos -n build/cmake/data.img`, `bxos_fat.py ls build/cmake/data.img:/` 통과.
- **Phase 2 (경로 파싱 / 해석) 코드 작업 완료**:
  - [bootpack.h](../harib27f/haribote/bootpack.h)에 `MAX_PATH=128`, `FS_MAX_DEPTH=16`, `FS_RESOLVE_*` 에러 코드, `fs_resolve_path()` 프로토타입 추가.
  - [fs_fat.c](../harib27f/haribote/fs_fat.c)에 절대/상대 path, `.`/`..`, 컴포넌트별 8.3 packing, 마지막 leaf 분리, leaf 존재 여부 조회 구현.
  - [console.c](../harib27f/haribote/console.c)에 임시 디버그 명령 `resolve <path>` 추가. 출력: parent cluster, leaf 8.3 이름, found/attr/cluster/size.
  - 검증: `cmake --build build/cmake --target kernel`, `cmake --build build/cmake`, `fsck_msdos -n build/cmake/data.img` 통과.
- **Phase 3 (mkdir / rmdir + 콘솔 명령) 코드 작업 완료**:
  - [bootpack.h](../harib27f/haribote/bootpack.h)에 `fs_mkdir()`/`fs_rmdir()` 및 `cmd_mkdir()`/`cmd_rmdir()` 프로토타입 추가.
  - [fs_fat.c](../harib27f/haribote/fs_fat.c)에 디렉터리 cluster 초기화(`.`/`..`), 빈 디렉터리 검사, FAT chain 해제, `fs_mkdir()`/`fs_rmdir()` 구현.
  - [console.c](../harib27f/haribote/console.c)에 `mkdir <path>`/`rmdir <path>` built-in 추가.
  - 검증: `cmake --build build/cmake --target kernel`, `cmake --build build/cmake`, `fsck_msdos -n build/cmake/data.img` 통과.
- **Phase 4 (cwd + 기존 명령 path 화) 코드 작업 완료**:
  - [bootpack.h](../harib27f/haribote/bootpack.h)에 `cwd_clus`/`cwd_path`를 `struct TASK`와 `struct CONSOLE`에 추가하고, `FS_FILE` path-aware 파일 핸들 구조를 추가.
  - [bootpack.c](../harib27f/haribote/bootpack.c) `open_constask()`가 부모 콘솔 cwd를 자식 task 초기 cwd로 복사.
  - [fs_fat.c](../harib27f/haribote/fs_fat.c)에 `fs_data_open_path()`/`fs_data_create_path()`/`fs_file_read()`/`fs_file_write()`/`fs_file_truncate()`/`fs_file_unlink()` 추가.
  - [console.c](../harib27f/haribote/console.c)에 `cd`/`pwd`, path-aware `dir`/`touch`/`rm`/`cp`/`mv`/`echo`/`mkfile`, cwd 우선 + root fallback 앱 검색 추가.
  - 검증: `cmake --build build/cmake --target kernel`, `cmake --build build/cmake`, `fsck_msdos -n build/cmake/data.img`, `git diff --check` 통과.
- **Phase 5 (사용자 API + HE2 앱) 코드 작업 완료**:
  - [console.c](../harib27f/haribote/console.c) syscall `api_fopen`/`api_fopen_w`/`api_fdelete`를 task cwd 기준 path-aware API로 변경하고 `api_getcwd`(edx=31) 추가.
  - 앱 실행 직전 HE2/HRB task cwd를 부모 콘솔 cwd로 동기화.
  - [he2/libbxos](../he2/libbxos)에 `api_getcwd()` 및 `bx_getcwd()` wrapper 추가.
  - legacy [apilib.h](../harib27f/apilib.h), [apilib/api031.nas](../harib27f/apilib/api031.nas), apilib Makefile 갱신.
  - [harib27f/pwd/pwd.c](../harib27f/pwd/pwd.c) 추가, [CMakeLists.txt](../CMakeLists.txt)에 `pwd.he2` 추가.
  - 검증: `cmake --build build/cmake --target kernel`, `cmake --build build/cmake`, `fsck_msdos -n build/cmake/data.img`, `bxos_fat.py ls build/cmake/data.img:/`에서 `PWD.HE2` 확인, `git diff --check` 통과.
- 남은 확인: QEMU 콘솔에서 root 한 단계 명령(`dir`, `cp`, `mv`, `rm`, `touch`, `echo > x`, `mkfile`) 대화형 회귀 확인.
- 남은 확인: QEMU 콘솔에서 `resolve /`, `resolve tetris.he2`, `resolve nofile`, 추후 mkdir 이후 `resolve /sub/../x` 같은 대화형 확인.
- 남은 확인: QEMU 콘솔에서 `mkdir /sub`, `mkdir /sub/inner`, `rmdir /sub` 거부, `rmdir /sub/inner`, `rmdir /sub` 흐름 확인. 호스트 `mount -t msdos`/`ls -laR` 확인도 남음.
- 남은 확인: QEMU 콘솔에서 `cd /sub`, `pwd`, `touch a.txt`, `dir /sub`, `cp /sub/a.txt b.txt`, `start pwd` cwd 상속 등 Phase 4 시나리오 확인.
- 남은 확인: QEMU 콘솔에서 `cd /sub`, `pwd.he2`, `echo.he2 hi > a.txt`, `fdel.he2 ../top.txt` 등 Phase 5 syscall 시나리오 확인.

## 3. 확정된 핵심 결정 (재확인용)

[work2.md §2](work2.md) 표가 정본. 요약:

- **경로 구분자**: `/` (Unix 스타일). DOS `\` 미지원.
- **절대 + 상대 경로** 둘 다. `/foo`, `foo/bar`, `./foo`, `../foo`.
- **루트 표기**: `/`. 드라이브 prefix 없음 (단일 마운트).
- **이름**: 컴포넌트당 8.3, LFN 미지원.
- **MAX_PATH = 128 바이트**, 컴포넌트 깊이 ≤ 16.
- **`.`/`..`**: 표준 FAT — subdir 첫 cluster 의 entry[0]=`.`, entry[1]=`..`. parent 가 root 면 `..` 의 first cluster = 0.
- **cwd**: per-task, `struct CONSOLE` 에 `cwd_clus` + `cwd_path[128]`. 자식 console/앱은 부모 값 상속.
- **`cd`/`pwd`**: 콘솔 built-in 만. 앱은 `api_getcwd` 만 노출, `api_chdir`/`api_mkdir`/`api_rmdir` 는 도입 안 함.
- **rmdir**: 빈 디렉터리만. recursive 삭제 없음.
- **mkdir**: 한 단계씩 (`mkdir -p` 없음).
- **타임스탬프**: 0 으로 채움 (RTC ABI 부재).
- **호스트 도구**: `bxos_fat.py` 가 path 다단계 + `mkdir`/`rmdir` 지원. `mkfat12.py` 는 root only 유지.

## 4. Phase 한눈에 보기

| Phase | 분량 | 핵심 산출물 |
|---|---|---|
| 0. 결정 / 인터페이스 | 1d | 본 문서 + work2.md (☑ 완료) |
| 1. 디렉터리 추상화 | 2d | `struct DIR_ITER` + `dir_*` 함수, root/subdir 공용 코드 경로. **코드 완료, QEMU 대화형 회귀 확인 남음**. |
| 2. 경로 파싱 / 해석 | 1d | `fs_resolve_path()` — 절대/상대, `.`/`..`, 8.3 packing. **코드 완료, QEMU 디버그 명령 확인 남음** |
| 3. mkdir / rmdir + 콘솔 | 2d | `fs_mkdir`/`fs_rmdir`, 콘솔 `mkdir <path>`/`rmdir <path>`. **코드 완료, QEMU/호스트 마운트 확인 남음** |
| 4. cwd + 기존 명령 path 화 | 2d | `cd`/`pwd`, `dir`·`cp`·`mv`·`rm`·`touch`·`echo`·`mkfile` 의 path 인자. **코드 완료, QEMU 확인 남음** |
| 5. 사용자 API + HE2 앱 | 1.5d | path 받는 fopen/fopen_w/fdelete, 신규 `api_getcwd` (edx=31), `pwd.he2`. **코드 완료, QEMU 확인 남음** |
| 6. 호스트 도구 | 1.5d | `bxos_fat.py` path 다단계 + `mkdir`/`rmdir` |
| 7. 문서 / 마무리 | 0.5d | storage.md / BXOS-COMMANDS.md / README / SETUP-MAC 갱신 |

총 **9~11 작업일**. Phase 6 은 Phase 3 끝나면 병행 가능.

## 5. 코드 길잡이

| 영역 | 주 변경 파일 |
|---|---|
| 커널 FS | [harib27f/haribote/fs_fat.c](../harib27f/haribote/fs_fat.c), [harib27f/haribote/bootpack.h](../harib27f/haribote/bootpack.h) |
| 콘솔 | [harib27f/haribote/console.c](../harib27f/haribote/console.c) (cmd_*, hrb_api), [harib27f/haribote/bootpack.c](../harib27f/haribote/bootpack.c) (console init/inherit) |
| 사용자 API (HE2) | [he2/libbxos/include/bxos.h](../he2/libbxos/include/bxos.h), [he2/libbxos/src/syscall.c](../he2/libbxos/src/syscall.c) |
| 사용자 API (legacy HRB) | [harib27f/apilib.h](../harib27f/apilib.h), [harib27f/apilib/](../harib27f/apilib/) (api031.nas 추가) |
| 검증용 앱 | [harib27f/pwd/](../harib27f/) (신규 — `pwd.he2`) |
| 호스트 도구 | [tools/modern/bxos_fat.py](../tools/modern/bxos_fat.py) |
| 빌드 | [CMakeLists.txt](../CMakeLists.txt) — `BXOS_HE2_APPS_BASIC` 에 `pwd` 추가 |
| 문서 | [BXOS-COMMANDS.md](../BXOS-COMMANDS.md), [_doc/storage.md](storage.md), [README.utf8.md](../README.utf8.md), [SETUP-MAC.md](../SETUP-MAC.md) |

## 6. 빠른 빌드/실행 치트시트 (work1 과 동일)

```bash
# 첫 설정 (한 번)
cmake -S . -B build/cmake

# 전체 빌드 (kernel-img + data-img)
cmake --build build/cmake

# 부팅 (build/cmake/data.img 가 있으면 자동으로 -hda 부착)
./run-qemu.sh
```

## 7. 함정으로 미리 알아둘 것

work2.md §5 가 정본. 강조:

- **디렉터리 cluster 는 zero-fill 필수** — FAT EOC 만 박고 데이터 cluster 안 비우면 garbage 가 entry 로 보여 무한 루프.
- **root `..` = 0 규약** — host 도구와 게스트가 같이 따라야 함. `0` 을 잘못 데이터 cluster 로 해석하면 디스크 손상으로 보임.
- **8.3 packing 일관** — `Foo.TXT` 와 `FOO.TXT` 가 같은 entry 로 매칭. 컴포넌트별 적용.
- **cwd 무효화** — rmdir 가 호출 task 의 cwd 또는 그 조상이면 거부.
- **타임스탬프 0 통일** — host/guest 모두 0 으로 박아 byte-identical 검증 가능하게.
- **MAX_PATH 일관** — 콘솔/syscall/cwd_path 모두 128B. 초과 시 즉시 에러.

## 8. 작업하지 말아야 할 것

- LFN, `mkdir -p`, `rm -r`, recursive 동작 일체 — work2 범위 밖.
- `api_chdir`/`api_mkdir`/`api_rmdir` syscall 도입 — 콘솔 built-in 으로 충분.
- 드라이브 prefix (`C:/foo`) — 단일 마운트라 미도입.
- 빌드 시점 서브디렉터리 자동 채우기 (`mkfat12.py` 확장) — 후속 작업.
- 타임스탬프 / RTC ABI — 별도 작업으로 분리.

## 9. 시작 명령

Phase 6 로 들어가면 됨. 다음 PR 단위 제안:

> "`tools/modern/bxos_fat.py` 에 다단계 path resolution, `mkdir`/`rmdir`, subdir-aware `ls`/`cp`/`rm` 을 추가한다."

QEMU 대화형 콘솔 회귀 확인을 먼저 끝내면 더 좋고, 그 뒤 Phase 6 (호스트 도구) 로 진입한다.
