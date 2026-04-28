# work1 인수인계 — 다음 세션용 컨텍스트

이 문서는 [_doc/work1.md](work1.md) 작업을 **새 세션에서 처음 보는 사람(또는 새 Claude)** 이 끊김 없이 이어받기 위한 단일 진입점이다. 먼저 work1.md 본문을 읽고, 그다음 이 문서로 현재 위치와 다음 행동을 파악하면 된다.

---

## 1. 한 줄 요약

BxOS(haribote 계열 취미 OS) 에 **쓰기 가능한 디스크 파일시스템**을 도입해, OS 커널 이미지와 사용자 앱 디스크를 **빌드 시점에 분리**하고, 호스트에서 앱을 개별 복사·삭제할 수 있게 만드는 것이 목표.

## 2. 현재 위치 (2026-04-28 기준)

- **Phase 0, 1, 2 완료**. Phase 3 (FAT16 읽기 경로 통합) 착수 직전.
- HE2(.he2) 포맷이 새로 도입되어, 빌드 이미지에 **.hrb 는 더 이상 포함되지 않고 .he2 앱 20개만** 들어감.
- 빌드 산출물이 **두 갈래로 분리**됨:
  - `build/cmake/haribote.img` — 1.44MB FAT12 부팅 FDD (`HARIBOTE.SYS` + `NIHONGO.FNT` 만)
  - `build/cmake/data.img` — 32MB FAT16 데이터 HDD (HE2 앱 + 데모 데이터)
- **ATA PIO 드라이버 동작**. 부팅 시 `ata_init()` 이 master/slave IDENTIFY 결과를 캐시 (`ata_drive_info[]`).
  - 콘솔 `disk` 명령으로 확인 가능. LBA 0 read 도 검증됨 (OEM='HARIBOTE', sig=0xAA55).
- 단, 아직 **FAT16 파싱은 없음** — 앱 실행은 여전히 메모리 FDD 이미지에서만 가능. data.img 에서 파일을 읽어 실행하려면 Phase 3 필요.

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
| [CMakeLists.txt](../CMakeLists.txt) §4, [tools/modern/Makefile.modern](../tools/modern/Makefile.modern) | `KERNEL_C_NAMES` 에 `ata` 등록. |

**아직 손대지 않은 것** (의도적, Phase 3 이후 작업):
- FAT 파싱은 여전히 [file.c](../harib27f/haribote/file.c) 의 메모리 이미지 전제 그대로. ATA 사용 안 함.
- 데이터 디스크에서 파일 읽기 / 앱 실행 — Phase 3 작업.

## 4. 확정된 핵심 결정 (재확인용)

work1.md §2 표가 정본. 요약:

- 블록 장치: **ATA PIO 28-bit LBA**
- 부팅: **FDD 유지**, HDD 는 데이터 전용
- FS: **FAT16** (FAT12 호환 한 갈래에 포함). FAT32 는 범위 밖.
- 데이터 디스크 크기: **32MB**, 기본 경로 **`build/cmake/data.img`**
- 쓰기 정책: **write-through**
- 게스트 드라이브: **`A:` = FDD, `C:` = HDD**, 콘솔 기본은 `C:` (Phase 3 도입)
- 호스트 도구: **자체 Python** (외부 mtools 의존 없음)

## 5. 다음 작업 (Phase 3 — FAT16 읽기 경로 통합)

work1.md §3 Phase 3 그대로. 요지:

현재 [file.c](../harib27f/haribote/file.c) 는 "FAT/루트디렉터리가 메모리 `ADR_DISKIMG` 에 이미 올라와 있다"는 전제로 동작한다. 이걸 ATA 기반 FAT16 마운트로 일원화한다.

1. **`harib27f/haribote/fs_fat.c` 신설** (또는 file.c 리팩터링):
   - 마운트 구조체: `struct FS_MOUNT { drive, BPB 캐시, FAT 캐시, 루트 시작 LBA, 데이터 시작 LBA, fs_type(12/16) }`
   - `fs_mount(drive, *mnt)` — BPB 한 섹터 read → 파라미터 파싱 → FAT 한 카피만 캐시 (32MB FAT16 의 FAT = 32KB, 메모리 OK)
   - `fs_open(mnt, path)` / `fs_read(fh, buf, n)` / `fs_close(fh)` / `fs_readdir(mnt, callback)`
   - FAT12/FAT16 분기는 cluster_count 로 자동 (FAT12 < 4085 < FAT16)
2. **두 마운트 동시 지원**:
   - `A:` = 부팅 FDD (기존 메모리 이미지 그대로 둘지, ATA 인터페이스로 통일할지는 결정 필요. 권장: FDD 도 ATA 비슷한 추상화 layer 통과)
   - `C:` = HDD ATA, FAT16 (Phase 2 의 `ata_drive_info[0]` 로 자동 마운트)
   - 콘솔 기본 드라이브 = `C:` (work1.md §2 결정)
3. **기존 호출부 교체**: [console.c](../harib27f/haribote/console.c) 의 `cmd_dir`/`cmd_app` 등에서 `(struct FILEINFO *) (ADR_DISKIMG + 0x002600)` 직접 접근하던 코드를 `fs_readdir`/`fs_open` 으로 변경. [bootpack.c](../harib27f/haribote/bootpack.c) HariMain 의 `nihongo.fnt` 로딩도 동일.
4. **검증**: `dir` 명령이 data.img 내용(HE2 앱 20개 + 데모 데이터 8개)을 표시하고, `winhelo` 등이 실제로 실행되어야 함. `disk` 명령(Phase 2의 임시) 은 그대로 두거나 더 이상 필요 없으면 제거.

**Phase 3 완료 기준**: 부팅 후 `dir` 로 데이터 디스크 파일 목록이 보이고, `winhelo`/`tetris` 등 HE2 앱이 정상 실행됨. 이전엔 빈 이미지였던 FDD 에 앱이 없어도 모든 게 동작.

**참고할 출발점**:
- mkfat12.py 의 `FsParams` 가 BPB 레이아웃의 정본 (offset 들 일치 확인)
- 현재 file.c 의 `file_readfat()` / `file_search()` / `file_loadfile2()` 가 FAT12 레퍼런스 — FAT16 버전은 12bit packed 디코딩만 빼면 거의 동일
- `_doc/work1.md` §5 함정 표 — 특히 "기본 드라이브 변경" 호환성 항목

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
| 파일시스템 | [harib27f/haribote/file.c](../harib27f/haribote/file.c) | ← Phase 3. 다음 작업 진입점. `fs_fat.c` 로 리팩터링 + ATA 기반 마운트. |
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
