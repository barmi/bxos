# work2 — 서브디렉터리 / 경로 / cwd 지원

## 1. 배경 / 목표

### 현재 상태 (work1 종료 시점)
- 데이터 디스크 [build/cmake/data.img](build/cmake/data.img) 는 FAT16 32MB, **루트 디렉터리만** 사용. 모든 파일이 한 단계에 평평하게 들어감.
- 콘솔 명령 (`dir`, `cp`, `mv`, `rm`, `touch`, `echo > file`, `mkfile`, `type`) 과 사용자 API (`api_fopen`, `api_fopen_w`, `api_fwrite`, `api_fdelete`) 모두 **8.3 단일 이름** 만 받는다. 슬래시(`/`) 가 있으면 잘못된 이름으로 처리됨.
- [harib27f/haribote/fs_fat.c](../harib27f/haribote/fs_fat.c) 는 root 디렉터리 캐시(`root_cache`, `root_entries`, `root_lba`) 를 직접 다루고, 일반 디렉터리(클러스터 체인 위에 올라간) 추상화가 없다.
- 호스트 도구 [tools/modern/bxos_fat.py](../tools/modern/bxos_fat.py) 도 root 한 단계만 지원.

### 이번 작업의 목표
1. **경로 표현 도입** — `/foo/bar.txt`, `../baz` 같은 다단계 경로를 OS / 콘솔 / 사용자 API / 호스트 도구 전부에서 받는다.
2. **mkdir / rmdir** — 콘솔 명령 + 사용자 API + 호스트 도구로 디렉터리를 생성/삭제할 수 있게.
3. **cwd (현재 작업 디렉터리)** — per-task 상태로 도입, `cd <path>` / `pwd` 콘솔 명령. 자식 콘솔/앱은 부모의 cwd 를 상속.
4. 기존 명령(`dir`/`cp`/`mv`/`rm`/`touch`/`echo`/`mkfile`/`type`/앱 검색) 모두 절대·상대 경로 수용.
5. 호스트 도구 [tools/modern/bxos_fat.py](../tools/modern/bxos_fat.py) 도 동일하게 — `data.img:/sub/file`, `mkdir`, `rmdir` 서브명령.

## 2. 설계 결정 사항 (확정 — 2026-04-28)

| 항목 | 결정 | 비고 |
|---|---|---|
| 경로 구분자 | **`/`** (forward slash) | DOS `\` 는 미지원. Unix 스타일이 콘솔 입력 더 편함. |
| 절대 / 상대 경로 | 둘 다 | `/foo/bar` = 절대, `foo/bar`·`./foo`·`../foo` = cwd 기준 상대 |
| 루트 표기 | `/` | `pwd` 출력에서 root 는 `/` |
| 드라이브 prefix | **없음** | 데이터 디스크(`C:`) 만 단일 마운트라 prefix 없음. 미래 확장 시 `C:/foo` 형태 추가 가능. |
| 이름 길이 | **8.3 (LFN 미지원 유지)** | 한 컴포넌트당 8.3. 단순 절단·대문자 변환. work1 정책 유지. |
| 경로 최대 길이 | **128 바이트** | `cwd_path[]` + 인자 합쳐 안전한 상한. 초과 시 입력 거부. |
| `.` / `..` 의미 | **표준 FAT** | 새 디렉터리 첫 cluster 의 entry[0]=`.`, entry[1]=`..`. parent 가 root 면 `..` 의 first cluster = 0 (FAT 규약). |
| cwd 저장 | per-task **`struct CONSOLE`** | `cwd_clus` (uint, 0 = root) + `cwd_path[128]` (display 용). 자식 콘솔/앱은 부모 값을 복사 상속. |
| `cd` / `pwd` | 콘솔 **built-in only** | HE2 앱이 cd 해도 부모 콘솔에 안 돌아옴 → 앱 syscall 로는 의미 없음. 앱은 `api_getcwd` 만 노출. |
| 사용자 API | `api_fopen`/`api_fopen_w`/`api_fdelete` 의 인자가 path 문자열이 되도록 일반화 + `api_getcwd` 신설 | edx 번호 보존, 의미만 확장 (root 단일 이름은 그대로 동작). |
| rmdir 정책 | **빈 디렉터리만** 삭제 | recursive `rm -r` 는 범위 밖. `.`/`..` 외 모든 슬롯이 free(0x00) 또는 deleted(0xE5) 일 때만 허용. |
| mkdir 정책 | parent 가 존재해야 함 (`mkdir -p` 미지원) | recursive 생성은 콘솔에서 두 번 치는 것으로 갈음. |
| 디렉터리 cluster 확장 | 일반 파일과 동일하게 FAT chain 으로 grow | 한 cluster (= 64 entries / 2KB) 가 차면 다음 cluster 할당, FAT EOC 갱신, 새 cluster zero-fill. |
| 디렉터리 entry 검색 시 | free / deleted 슬롯 **재사용** | rmdir 후 빈 슬롯에 새 파일이 들어가도록. |
| 타임스탬프 | **0 으로 채움** | 현재 커널에 RTC ABI 가 없음. 향후 별도 작업으로 도입. fsck_msdos 는 0 타임스탬프 허용. |
| 호스트 도구 | `bxos_fat.py` 에 `mkdir`/`rmdir` 서브명령 + 모든 path 인자에 다단계 지원. `mkfat12.py` 는 일단 root only 유지 | 빌드 시점부터 서브디렉터리 채우기는 후속 작업. |

## 3. 작업 단계

체크박스(☐)는 PR 경계를 표시한다.

### Phase 0 — 결정 / 인터페이스 확정 (1일)
- ☐ 2장 결정 표 잠금. 본 문서 + work2-handoff.md 에 반영.
- ☐ 새 syscall 번호 예약: `api_getcwd` = edx 31. (`api_chdir` 는 도입하지 않음 — cwd 는 콘솔 built-in 만.)
- ☐ 새 콘솔 prompt 정책: 기본 `>`, `pwd` 출력만 path 표시. (prompt 자체에 cwd 박는 건 화면 폭 좁아 보류.)
- ☐ 신규 헤더 선언 자리 잡기: `bootpack.h` 에 `MAX_PATH = 128`, `struct DIR_ITER`, `fs_resolve_path`, `fs_mkdir`, `fs_rmdir`, `fs_data_open_dir` 등.

### Phase 1 — 디렉터리 추상화 (일반 directory I/O) (2일)
**목표**: 기존 root 한정 함수들이 root + 일반 cluster chain 위 디렉터리 둘 다 다루도록 재구성. 외부 동작은 회귀 0.

- ☑ `struct DIR_ITER` 도입 — `dir_clus` (0 = root), `cur_lba`, `cur_offset_in_sector`, `cur_cluster_offset`. root 는 `root_lba..root_lba+root_sectors`, subdir 는 cluster chain.
- ☑ `dir_iter_open(clus)`, `dir_iter_next(*entry, *slot_addr)`, `dir_iter_close()`.
- ☑ `dir_find(parent_clus, name83[11], *finfo, *slot_addr)` — 이름으로 entry 찾기. root + subdir 공용.
- ☑ `dir_alloc_slot(parent_clus, *slot_addr)` — 빈 슬롯 찾기 + (subdir 한정) 클러스터 자동 확장.
- ☑ `dir_write_slot(slot_addr, entry)` — 해당 LBA 섹터 read-modify-write + ATA flush. FAT1/FAT2 동기화 패턴과 동일한 트랜잭션.
- ☑ 기존 `fs_data_search`, `fs_data_create`, `fs_data_unlink`, `sync_root_entry` 등을 위 추상화 위로 옮김. 외부 시그니처는 유지.
- ☐ 회귀 검증:
  - ☐ 기존 콘솔 명령 (`dir`, `cp`, `mv`, `rm`, `touch`, `echo > x`, `mkfile`) 모두 root 한 단계에서 work1 시점과 같은 결과. (QEMU 대화형 확인 필요)
  - ☑ `fsck_msdos -n` 통과.

### Phase 2 — 경로 파싱 / 해석 (1일)
- ☑ `fs_resolve_path(start_clus, path, *parent_clus, leaf_name83[11], *leaf_finfo)` 구현:
  - ☑ `path` 가 `/` 로 시작하면 root 부터, 아니면 `start_clus` 부터.
  - ☑ 컴포넌트별 `.`/`..` 처리. `..` 는 parent dir 의 entry[1] 로 따라가기. root 의 `..` = root.
  - ☑ 마지막 컴포넌트만 leaf 로 분리해서 caller 가 후속 처리(존재 여부, 생성 등).
  - ☑ 8.3 변환 (`pack_83_name`) 컴포넌트별 적용.
  - ☑ 길이 초과 / 잘못된 컴포넌트 / 중간 경로가 디렉터리가 아님 등 에러 코드 반환.
- ☐ 단위 테스트 대신 콘솔에서 임시 디버그 명령 `resolve <path>` 로 출력 확인 (Phase 3 직전에 제거). (명령 추가 완료, QEMU 대화형 확인 필요)

### Phase 3 — mkdir / rmdir + 콘솔 명령 (2일)
- ☑ `fs_mkdir(start_clus, path)`:
  - ☑ resolve → parent_clus + leaf_name 확보, leaf 가 이미 존재하면 -EEXIST.
  - ☑ 빈 cluster 1 개 할당 → zero-fill → entry[0]=`.` (clus = 새 cluster), entry[1]=`..` (clus = parent_clus, root 면 0).
  - ☑ parent 에 새 directory entry 추가 (attr=0x10, first cluster = 새 cluster, size = 0).
  - ☑ FAT 체인 종료 마크 + FAT1/FAT2 동기화 + parent slot flush + cluster flush 모두 write-through.
- ☑ `fs_rmdir(start_clus, path)`:
  - ☑ resolve → 존재해야 하고 attr=0x10 이어야 함.
  - ☑ dir 의 모든 entry 확인 — `.`/`..` 외 free(0x00) / deleted(0xE5) 만 있을 때만 진행.
  - ☑ parent 의 슬롯을 0xE5 로 마크 + FAT chain 해제 (모든 cluster 0 으로) + flush.
- ☑ 콘솔 추가: `mkdir <path>`, `rmdir <path>`.
- ☐ 검증 시나리오:
  - ☐ `mkdir /sub`, `mkdir /sub/inner`, `dir /sub` 으로 inner 보임, `rmdir /sub` 은 거부, `rmdir /sub/inner` → `rmdir /sub` 순으로 가능. (QEMU 대화형 확인 필요)
  - ☑ 각 단계 후 `fsck_msdos -n` 통과. ☐ macOS `mount -t msdos` 마운트 후 `ls -laR` 로 호환성 확인.

### Phase 4 — cwd + 기존 명령 path 화 (2일)
- ☐ `struct CONSOLE` 에 `unsigned int cwd_clus`, `char cwd_path[MAX_PATH]` 추가. console 생성 시 0/`"/"` 로 초기화. `start <cmd>` / `ncst <cmd>` 로 자식 콘솔 만들 때 부모 값을 복사.
- ☐ 콘솔 `cd <path>` / `pwd` built-in 추가. `cd` 는 resolve 후 디렉터리인지 확인하고 `cwd_clus`/`cwd_path` 갱신. `cwd_path` 는 정규화된 표준 형태로 유지 (`.`/`..` 풀어서, `//` 정리).
- ☐ 기존 명령 인자가 path 가능하도록:
  - `dir [<path>]` — 인자 없으면 cwd 의 디렉터리 출력.
  - `cp <src> <dst>`, `mv <src> <dst>`, `rm <path>`, `touch <path>`, `echo <text> > <path>`, `mkfile <path> <bytes>`, `type <path>` (※ type 은 현재 HE2 앱만 — 콘솔 빌트인 추가 여부 별도 결정. 일단 보류.)
  - 앱 검색 (`app_find`) 도 `<cwd>` 와 root 둘 다 살피도록: `./foo`, `/bin/foo` 같은 경로 입력을 받으면 그대로 resolve. 인자 없는 그냥 이름은 cwd 우선, 없으면 root 폴백.
- ☐ 회귀: cwd 가 `/` 인 상태에서 work1 시점 동작 모두 동일.
- ☐ 검증:
  - `cd /sub`, `dir`, `touch a.txt`, `cd /`, `dir /sub` → `A.TXT` 보임.
  - `cd ..`, `cd .`, `cd /` 정상 동작.
  - `cp /sub/a.txt b.txt` 같은 mixed-prefix 경로 OK.

### Phase 5 — 사용자 API + HE2 앱 (1.5일)
- ☐ syscall 디스패처: `api_fopen` / `api_fopen_w` / `api_fdelete` 가 받는 path 인자를 path resolution 경유로 처리. (edx 번호 보존, 의미만 확장.) cwd 는 호출 task 의 콘솔 cwd 를 사용 (앱 task 는 부모 콘솔 cwd 상속).
- ☐ 신규 syscall: `api_getcwd(buf, n)` — `cwd_path` 를 buf 에 복사. edx 번호 31 (work1 의 28~30 다음).
- ☐ HE2 [he2/libbxos](../he2/libbxos) 에 `bx_getcwd(buf, n)` wrapper 추가. legacy HRB `apilib` 에 `api031.nas` 도 추가 (전 작업과 일관).
- ☐ 검증용 HE2 앱 추가:
  - `mkdir.he2 <path>` — `api_mkdir` 가 없으므로... 이건 어떻게? **결정**: 앱이 직접 디렉터리 만들 일은 적고, syscall 추가 비용보다 콘솔 명령으로 충분. **`api_mkdir`/`api_rmdir` 는 도입하지 않는다.** Phase 5 에서 만드는 HE2 앱은 path 인자 검증용:
  - `pwd.he2` — `bx_getcwd` 호출 결과 출력.
  - 기존 `echo.he2 <text> > <path>`, `touch.he2 <path>`, `fdel.he2 <path>` 가 path 받게 자연스럽게 확장됨 (소스 변경 없이 syscall 의미 변경만으로 동작해야 함).
- ☐ 검증:
  - `cd /sub`, `echo.he2 hi > a.txt` → `/sub/a.txt` 에 저장됨, 호스트에서 확인.
  - `pwd.he2` 가 `/sub` 출력.
  - `fdel.he2 ../top.txt` (상대경로) 동작.

### Phase 6 — 호스트 도구 (1.5일)
- ☐ [tools/modern/bxos_fat.py](../tools/modern/bxos_fat.py):
  - 모든 path 인자가 `image:/a/b/c` 다단계 받게 — `_resolve_path` 구현.
  - `mkdir <image:/path>` / `rmdir <image:/path>` 서브명령 추가.
  - `ls <image:/path>` 가 디렉터리면 그 디렉터리 entries 출력 (현재 root 만).
  - `cp` / `rm` 도 다단계 path 수용. host↔image 양방향 모두.
- ☐ FAT16 디렉터리 cluster 할당 / `.`/`..` 초기화 / FAT chain 갱신을 게스트와 동일한 정책으로 (호환성 위해 byte-identical 한 BPB/엔트리 만들기).
- ☐ 검증:
  - 새 이미지 생성 → `mkdir /sub` → `cp HOST:tetris.he2 image:/sub/tetris.he2` → 호스트에서 다시 추출 후 `cmp` 일치.
  - QEMU 부팅 후 `dir /sub` 에서 호스트가 만든 디렉터리/파일 보이고, 게스트에서 `rm /sub/tetris.he2`, `rmdir /sub` 후 `fsck_msdos -n` 통과.
  - 역방향: 게스트에서 `mkdir`/파일 생성 → 호스트 `bxos_fat.py ls` 로 확인.

### Phase 7 — 문서 / 마무리 (0.5일)
- ☐ [_doc/storage.md](storage.md) 갱신 — 디렉터리 cluster 레이아웃, `.`/`..` 의미, cwd 모델, 호스트 도구 path 사용법.
- ☐ [BXOS-COMMANDS.md](../BXOS-COMMANDS.md) 갱신 — `mkdir`/`rmdir`/`cd`/`pwd`, 기존 명령의 path 인자 설명, `pwd.he2` 추가.
- ☐ [README.utf8.md](../README.utf8.md) / [SETUP-MAC.md](../SETUP-MAC.md) — `bxos_fat.py mkdir/rmdir` 예시 한 줄씩 추가.
- ☐ work2.md / work2-handoff.md 체크박스 닫음.

## 4. 마일스톤 / 검증 시나리오

| 끝난 시점 | 검증 |
|---|---|
| Phase 1 | 회귀: work1 시점 콘솔 명령 모두 그대로 동작. `fsck_msdos -n` 통과. 코드는 줄어들지 않더라도 root 와 subdir 가 같은 코드 경로. |
| Phase 2 | 콘솔 디버그 명령 `resolve /a/../b/c` → 정규화된 출력. 잘못된 경로는 에러. |
| Phase 3 | 콘솔에서 `mkdir`/`rmdir` 가능. `mount -t msdos` 로 호스트가 게스트가 만든 디렉터리 정상 인식. |
| Phase 4 | `cd` 로 cwd 이동, 기존 명령들이 cwd 기준 동작. `pwd` 정상. |
| Phase 5 | HE2 앱이 path 인자 받아 동작. `pwd.he2` 가 부모 콘솔의 cwd 출력. |
| Phase 6 | 호스트에서 만든 서브디렉터리/파일이 게스트에서 보이고, 그 반대도 OK. |
| Phase 7 | 문서가 새 모델을 반영. 신규 진입자가 막히지 않음. |

## 5. 위험 요소 / 함정

- **cluster #2 / `..` = 0 규약** — root 의 `..` 는 first_cluster_lo = 0 으로 박혀야 함. host 도구와 게스트 양쪽 일관. `0` 을 잘못 데이터 cluster 로 해석하면 디스크가 깨진 것처럼 보임.
- **디렉터리 cluster 확장 시 zero-fill 필수** — FAT 의 EOC 표시만 하고 cluster 내용을 안 비우면 garbage 가 entry 로 보여서 무한 루프. `dir_alloc_slot` 의 grow 경로에서 새 cluster 0 으로 채우는 것을 잊지 말 것.
- **rmdir 빈 검사** — `.`/`..` 두 슬롯을 무조건 통과하고, 그 뒤로 한 슬롯이라도 attr != 0 && attr != 0xE5 면 거부. 직전에 unlink 된 파일의 0xE5 슬롯은 비어있는 것으로 간주.
- **path resolution 의 8.3 packing** — 컴포넌트별로 `pack_83_name` 적용. 입력 `Foo.TXT` 와 `FOO.TXT` 가 같은 entry 로 매칭되어야 함. 디렉터리 컴포넌트도 마찬가지 (`/Sub/Foo.TXT` ↔ `/SUB/FOO.TXT`).
- **cwd 무효화** — `rmdir` 가 자기 자신/조상 디렉터리를 삭제하는 경우 거부. resolve 시 leaf 의 cluster 가 호출 task cwd_clus 의 조상 chain 에 있는지 검사.
- **호스트 도구와 게스트의 dir entry 형식 불일치** — date/time 필드를 0 으로 통일하지 않으면 `cmp` 가 안 맞음. 둘 다 0 으로 박는다.
- **MAX_PATH 초과** — 콘솔 입력 / syscall 인자 / cwd_path 모두 통일된 한도(128B). 초과 시 명확한 에러.
- **stack 사용량** — resolve 가 컴포넌트별 buffer 를 stack 에 잡으면 깊은 path 에서 overflow 위험. 깊이 상한도 같이 (≤ 16).

## 6. 범위 외 (이번 작업에서 안 하는 것)

- LFN (긴 파일명) — 컴포넌트당 8.3 유지.
- `mkdir -p` (recursive 생성) — 한 단계씩.
- `rm -r` / `cp -r` (recursive 삭제·복사) — 빈 디렉터리만 rmdir.
- 디렉터리 attribute 외 권한 / 소유자 / hidden flag.
- 타임스탬프 채우기 (RTC ABI 부재).
- `mkfat12.py` 의 빌드 시점 서브디렉터리 채우기 — 일단 root only 유지. 필요하면 후속.
- `api_chdir` / `api_mkdir` / `api_rmdir` syscall — 콘솔 built-in 으로 충분.
- 드라이브 prefix (`C:/foo`) — 단일 마운트라 미도입.
- 동시성 / 파일 잠금 — 단일 콘솔 환경 가정 유지.

## 7. 예상 일정

총 **9~11 작업일** (한 사람 풀타임 기준). Phase 1 (디렉터리 추상화) 가 가장 무겁고 위험. Phase 6 (호스트 도구) 는 Phase 3 끝나면 병행 가능.
