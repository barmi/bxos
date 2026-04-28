# work1 인수인계 — 다음 세션용 컨텍스트

이 문서는 [_doc/work1.md](work1.md) 작업을 **새 세션에서 처음 보는 사람(또는 새 Claude)** 이 끊김 없이 이어받기 위한 단일 진입점이다. 먼저 work1.md 본문을 읽고, 그다음 이 문서로 현재 위치와 다음 행동을 파악하면 된다.

---

## 1. 한 줄 요약

BxOS(haribote 계열 취미 OS) 에 **쓰기 가능한 디스크 파일시스템**을 도입해, OS 커널 이미지와 사용자 앱 디스크를 **빌드 시점에 분리**하고, 호스트에서 앱을 개별 복사·삭제할 수 있게 만드는 것이 목표.

## 2. 현재 위치 (2026-04-28 기준)

- **Phase 0, 1, 2, 3 완료**. Phase 4 (FAT16 쓰기 경로) 착수 직전.
- HE2(.he2) 포맷이 새로 도입되어, 빌드 이미지에 **.hrb 는 더 이상 포함되지 않고 .he2 앱 20개만** 들어감.
- 빌드 산출물이 **두 갈래로 분리**됨:
  - `build/cmake/haribote.img` — 1.44MB FAT12 부팅 FDD (`HARIBOTE.SYS` + `NIHONGO.FNT` 만)
  - `build/cmake/data.img` — 32MB FAT16 데이터 HDD (HE2 앱 + 데모 데이터)
- **ATA PIO 드라이버 + FAT16 read 경로 모두 동작**:
  - 부팅 시 `ata_init()` → IDENTIFY 캐시 → `fs_mount_data(0)` 가 BPB/FAT/루트 캐시
  - `dir` / 앱 실행 / `type <file>` / 사용자 앱의 `api_fopen` 모두 data.img 에서 작동
  - `winhelo`, `tetris` 등 HE2 앱이 ATA → FAT16 chain → tek 디컴프 → 실행까지 end-to-end 검증됨
- 아직 **쓰기 경로 없음**. 파일 생성/추가/삭제는 Phase 4 작업.

## 3. Phase 0 / Phase 1 에서 실제로 바뀐 것

**Phase 0** (분기 결정 + 부팅 스크립트):
| 파일 | 변경 |
|---|---|
| [_doc/work1.md](work1.md) | 2장 결정 사항 표를 "권장" → "확정"으로 잠금. Phase 0 체크박스 닫음. |
| [run-qemu.sh](../run-qemu.sh) | `--data`, `--no-data`, `BXOS_DATA_IMG` 추가. `build/cmake/data.img` 자동 부착. 부팅 이미지 기본도 `build/cmake/haribote.img` 우선, 없으면 `harib27f/haribote.img` 폴백. |

**Phase 1** (이미지 분리):
| 파일 | 변경 |
|---|---|
| [tools/modern/mkfat12.py](../tools/modern/mkfat12.py) | `--fs {fat12,fat16}` + `--size` 옵션. `FsParams` 데이터클래스로 BPB/FAT 폭 분기. FAT12 회귀 검증됨. |
| [CMakeLists.txt](../CMakeLists.txt) §12a/12b/13 | 단일 `image` 빌드를 `kernel-img` (FAT12 1.44MB) + `data-img` (FAT16 32MB) 로 분리. `bxos ALL`/`run` 둘 다 두 이미지 의존. |
| [_doc/work1.md](work1.md) | Phase 1 체크박스 닫음. |

**Phase 2** (ATA PIO 드라이버):
| 파일 | 변경 |
|---|---|
| [harib27f/haribote/ata.c](../harib27f/haribote/ata.c) (신규) | IDENTIFY / READ SECTORS / WRITE SECTORS / CACHE FLUSH. 폴링 + 타임아웃. |
| [harib27f/haribote/bootpack.h](../harib27f/haribote/bootpack.h) | `struct ATA_INFO`, ATA 함수 선언, `io_in16`/`io_out16` C 선언, `cmd_disk` 선언. |
| [harib27f/haribote/bootpack.c](../harib27f/haribote/bootpack.c) | `HariMain` 에서 `ata_init()` 호출 (PIC 초기화 직후). |
| [harib27f/haribote/console.c](../harib27f/haribote/console.c) | `disk` 명령 + `cmd_disk()` 추가. LBA 0 read 검증 포함. |
| [CMakeLists.txt](../CMakeLists.txt), [tools/modern/Makefile.modern](../tools/modern/Makefile.modern) | `KERNEL_C_NAMES` 에 `ata` 등록. |

**Phase 3** (FAT16 read 통합):
| 파일 | 변경 |
|---|---|
| [harib27f/haribote/fs_fat.c](../harib27f/haribote/fs_fat.c) (신규) | `struct FS_MOUNT` + `fs_mount_data` + `fs_data_search` + `fs_data_loadfile`. FAT12/16 자동 분기 (cluster_count). FAT/루트 메모리 캐시, 클러스터 데이터는 ATA on-demand. |
| [harib27f/haribote/bootpack.h](../harib27f/haribote/bootpack.h) | `FS_MOUNT` 구조체 + 함수 선언. |
| [harib27f/haribote/bootpack.c](../harib27f/haribote/bootpack.c) | `HariMain` memman 초기화 직후 `fs_mount_data(0)`. 중복된 memman_init 정리. |
| [harib27f/haribote/console.c](../harib27f/haribote/console.c) | `cmd_dir`, `app_find`, `app_subsystem`, `cmd_app`, `api_fopen` 의 ADR_DISKIMG 직접 접근을 새 fs_data_* 로 교체. |
| [CMakeLists.txt](../CMakeLists.txt), [tools/modern/Makefile.modern](../tools/modern/Makefile.modern) | `KERNEL_C_NAMES` 에 `fs_fat` 등록. |

**아직 손대지 않은 것** (의도적, Phase 4 이후 작업):
- 쓰기 경로 — `fs_create` / `fs_write` / `fs_unlink` 모두 미구현. ATA 측 `ata_write_sectors` 는 Phase 2 에서 만들어뒀음.
- 사용자 API 의 쓰기 syscall — `api_fwrite` / `api_fdelete` 등 미정의.
- file.c 의 FDD 경로(nihongo.fnt 로딩) 는 그대로 유지. ADR_DISKIMG 을 완전히 떼는 작업은 향후 cleanup.

## 4. 확정된 핵심 결정 (재확인용)

work1.md §2 표가 정본. 요약:

- 블록 장치: **ATA PIO 28-bit LBA**
- 부팅: **FDD 유지**, HDD 는 데이터 전용
- FS: **FAT16** (FAT12 호환 한 갈래에 포함). FAT32 는 범위 밖.
- 데이터 디스크 크기: **32MB**, 기본 경로 **`build/cmake/data.img`**
- 쓰기 정책: **write-through**
- 게스트 드라이브: **`A:` = FDD, `C:` = HDD**, 콘솔 기본은 `C:` (Phase 3 도입)
- 호스트 도구: **자체 Python** (외부 mtools 의존 없음)

## 5. 다음 작업 (Phase 4 — FAT16 쓰기 경로)

work1.md §3 Phase 4 그대로. 요지:

[harib27f/haribote/fs_fat.c](../harib27f/haribote/fs_fat.c) 에 쓰기 함수들을 추가하고, 콘솔 명령 + 사용자 API 도 함께 정비. `ata_write_sectors` 는 Phase 2 에서 이미 만들어뒀음.

1. **`fs_fat.c` 에 추가**:
   - `int fs_data_create(char *name)` — 빈 디렉터리 엔트리 생성, 클러스터는 아직 미할당. 같은 이름 있으면 에러.
   - `int fs_data_write(struct FILEINFO *finfo, int pos, const void *buf, int n)` — 필요 시 FAT 체인 확장(빈 클러스터 검색 + 링크), 데이터 cluster write, 디렉터리 엔트리의 size 갱신.
   - `int fs_data_unlink(struct FILEINFO *finfo)` — FAT 체인 해제(엔트리 0 으로) + 디렉터리 엔트리 첫 바이트 0xE5.
2. **트랜잭션 순서 (work1.md §5 함정)**:
   - 항상 데이터 cluster 먼저 → FAT 갱신 → 디렉터리 엔트리 갱신 순.
   - FAT 캐시 변경 → ata_write_sectors 로 FAT1/FAT2 둘 다 동기화.
   - write-through (캐시는 메모리에 두되, 모든 변경은 즉시 ATA flush).
3. **콘솔 명령 (검증용)**:
   - `touch <name>` — 빈 파일 생성
   - `rm <name>` — 삭제
   - 또는 `echo <text> > <file>` 형태도 가능 (파서 수정 필요).
4. **검증 시나리오**:
   - 콘솔에서 파일 생성 → `dir` 에 보임
   - QEMU 종료 후 호스트에서 `fsck_msdos -n build/cmake/data.img` 깨끗
   - 호스트에서 마운트 후 파일 내용 확인
   - 100KB 정도 큰 쓰기 → 다시 read 해서 일치 확인
   - 삭제 후 재부팅, 클러스터 재사용

**Phase 4 완료 기준**: 콘솔에서 파일 생성/삭제 후 재부팅해도 그 변화가 보존됨. fsck 통과.

**참고**:
- FAT 체인 확장: cluster 2 부터 순회하면서 fat_get(c)==0 인 첫 free 클러스터 찾기. 끝.
- 디렉터리 엔트리 갱신: root_cache 의 해당 엔트리 수정 → 그 엔트리가 속한 root 디렉터리 섹터 한 장만 ata_write_sectors 로 다시 쓰기.
- FAT 갱신: fat_cache 의 해당 엔트리 수정 → 그 엔트리가 속한 FAT 섹터 한 장만 FAT1/FAT2 둘 다 ata_write_sectors.
- 파일명 8.3 변환: file.c 의 file_search 코드 일부 재사용 가능 (대문자/공백 패딩 로직).

## 6. 빠른 빌드/실행 치트시트

```bash
# 첫 설정 (한 번)
cmake -S . -B build/cmake

# 전체 빌드 (kernel-img + data-img)
cmake --build build/cmake

# 개별 빌드
cmake --build build/cmake --target kernel-img   # build/cmake/haribote.img (FAT12 1.44MB)
cmake --build build/cmake --target data-img     # build/cmake/data.img     (FAT16 32MB)

# 부팅 (build/cmake/data.img 이 있으면 자동으로 -hda 부착)
./run-qemu.sh

# 데이터 디스크 명시 / 무시
./run-qemu.sh --data path/to/data.img
./run-qemu.sh --no-data
```

이미지 안 파일 목록 확인 (FAT12/FAT16 BPB 파싱):

```bash
python3 - <<'PY'
import struct, sys
for path in ('build/cmake/haribote.img', 'build/cmake/data.img'):
    try: d = open(path,'rb').read()
    except FileNotFoundError: continue
    bps  = struct.unpack_from('<H', d, 11)[0]
    spc  = d[13]
    rsvd = struct.unpack_from('<H', d, 14)[0]
    nfat = d[16]
    rent = struct.unpack_from('<H', d, 17)[0]
    fsz  = struct.unpack_from('<H', d, 22)[0]
    fst  = d[54:62].decode().strip()
    root_start = (rsvd + nfat * fsz) * bps
    root_secs  = (rent * 32 + bps - 1) // bps
    print(f'== {path}  [{fst}]  {len(d)//1024} KB ==')
    root = d[root_start:root_start + root_secs * bps]
    for i in range(0, len(root), 32):
        e = root[i:i+32]
        if e[0] in (0, 0xE5) or (e[11] & 0x08): continue
        n = e[0:8].decode('ascii','replace').rstrip() + '.' + e[8:11].decode('ascii','replace').rstrip()
        sz = struct.unpack_from('<I', e, 28)[0]
        print(f'  {n:14s} {sz:>8d}')
PY
```

QEMU 가 두 디스크를 인식하는지 확인 (모니터):

```bash
echo -e "info block\nquit" | qemu-system-i386 -m 32 -accel tcg -display none \
  -fda build/cmake/haribote.img -boot a -hda build/cmake/data.img -monitor stdio
# floppy0 + ide0-hd0 두 줄이 나와야 OK.
```

## 7. 코드 길잡이 (어디를 손대게 되는가)

| 영역 | 파일 | Phase 1~7 에서 할 일 |
|---|---|---|
| 호스트 이미지 빌더 | [tools/modern/mkfat12.py](../tools/modern/mkfat12.py) | ☑ Phase 1 완료(FAT16 추가). Phase 7: `bxos_fat.py` 로 분화 (cp/rm/ls 부분 갱신용). |
| 빌드 시스템 | [CMakeLists.txt](../CMakeLists.txt) | ☑ Phase 1 완료(타겟 분리). Phase 7: `install-app` 헬퍼. |
| 부팅 스크립트 | [run-qemu.sh](../run-qemu.sh) | ☑ Phase 0 완료. 추가 변경 불필요. |
| 디스크 드라이버 | [harib27f/haribote/ata.c](../harib27f/haribote/ata.c) | ☑ Phase 2 완료. ATA PIO 28-bit LBA, IDENTIFY/READ/WRITE/FLUSH. |
| 파일시스템 read | [harib27f/haribote/fs_fat.c](../harib27f/haribote/fs_fat.c) | ☑ Phase 3 완료. mount + read + tek 디컴프. FAT12/FAT16 자동 분기. |
| 파일시스템 write | (확장) [harib27f/haribote/fs_fat.c](../harib27f/haribote/fs_fat.c) | ← Phase 4. 다음 작업 진입점. create/write/unlink + FAT/dir 동기화. |
| 콘솔 명령 / API | [harib27f/haribote/console.c](../harib27f/haribote/console.c), [harib27f/haribote/bootpack.c](../harib27f/haribote/bootpack.c) | Phase 3, 5, 6. |
| 사용자 API (HE2) | [he2/libbxos/](../he2/libbxos/) | Phase 5. |
| 문서 | [BXOS-COMMANDS.md](../BXOS-COMMANDS.md), [README.utf8.md](../README.utf8.md), [SETUP-MAC.md](../SETUP-MAC.md) | Phase 8. |

## 8. 이 작업에서 함정으로 미리 알아둘 것

work1.md §5 가 정본. 강조 사항만 추리면:

- **ATA PIO 폴링은 hang 가능** — 타임아웃 + 디버그 창 로깅 필수.
- **FAT 쓰기 트랜잭션** — FAT1/FAT2 동기화 + 디렉터리 엔트리 갱신 순서 잘못되면 디스크가 부팅 후 깨진 채 보임.
- **기본 드라이브 변경** — `A:` → `C:` 전환은 호환성 깨짐. Phase 3 에서 `BXOS-COMMANDS.md` 갱신 같이.
- **LFN, 캐시, 멀티유저, 권한, MBR 파티션, ext2 등은 범위 밖**. 욕심내지 말 것.

## 9. 작업하지 말아야 할 것

- 새 README/문서를 만들지 말 것. 기존 [_doc/work1.md](work1.md), [BXOS-COMMANDS.md](../BXOS-COMMANDS.md), [README.utf8.md](../README.utf8.md), [SETUP-MAC.md](../SETUP-MAC.md) 를 갱신.
- HRB 빌드 경로(.hrb 앱들) 는 더 이상 IMG 에 안 들어가므로, 깨져도 신경 쓰지 말 것. 단 [cmake/HariboteApp.cmake](../cmake/HariboteApp.cmake) 의 헬퍼 함수는 호환성을 위해 그대로 둠.
- 기존에 삭제된 `build-mac.sh` / `build-modern.sh` 를 부활시키지 말 것 (의도된 삭제, 곧 커밋 예정).
