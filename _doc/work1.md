# work1 — 쓰기 가능한 파일시스템 도입 및 OS/앱 분리 빌드 계획

## 1. 배경 / 목표

### 현재 상태
- 부팅 매체는 **1.44MB FAT12 플로피 이미지(haribote.img)** 또는 ISO 한 개뿐.
- 부팅 시 IPL이 **플로피 전체를 메모리(`ADR_DISKIMG`)로 로드**해서 끝. 디스크 I/O 드라이버는 존재하지 않는다.
  - [harib27f/haribote/file.c:79](harib27f/haribote/file.c) `file_loadfile2()` 가 메모리에 올라간 이미지에서만 파일을 읽음.
- 그래서 모든 파일 접근은 **read-only**. 앱이 파일을 만들거나 수정할 수 없고, 콘솔에 `cp`/`rm`/`mkdir` 같은 명령도 없음.
- 빌드 시점에 [tools/modern/mkfat12.py](tools/modern/mkfat12.py) 가 모든 앱(.hrb)과 데이터 파일(.mml/.jpg/.fnt 등)을 묶어 단일 이미지를 생성. 앱을 하나 추가/수정하려면 매번 전체 이미지를 다시 빌드해야 함.

### 이번 작업의 목표
1. **블록 장치 + 쓰기 가능한 FAT 드라이버**를 추가해, 메모리 캐시가 아닌 실제 디스크에 파일을 만들고 수정할 수 있게 한다.
2. **빌드 산출물을 두 갈래로 분리**:
   - 커널 이미지(부팅용, 작고 자주 안 바뀜)
   - 사용자 데이터 디스크(앱/리소스, 자주 바뀜, 호스트에서 부분 갱신 가능)
3. 호스트 측에서 만들어진 `.hrb` 앱을 데이터 디스크에 **개별적으로 복사·삭제**할 수 있는 도구를 제공한다.
4. 게스트(BxOS) 내부에서도 동일한 작업(`cp`, `rm`, `mkdir`)이 가능하도록 콘솔 명령과 사용자 API를 늘린다.

## 2. 설계 결정 사항 (확정 — 2026-04-28)

| 항목 | 결정 | 비고 |
|---|---|---|
| 블록 장치 | **ATA PIO (28-bit LBA)** | QEMU `-hda` 로 붙고, 코드 200줄 안쪽. 실기 부팅도 가능. virtio는 PCI 스택 없어서 보류. |
| 부팅 매체 | **FDD 유지 + HDD 데이터** | 부팅은 기존 IPL 그대로 두고, 추가된 HDD를 데이터 디스크로만 사용 → 변경 영향 최소. |
| 데이터 디스크 FS | **FAT16** (FAT12 호환 코드 한 갈래에 포함) | FAT12 와 ~80% 코드 공유. FAT32 는 이번 작업 범위 밖. |
| 데이터 디스크 크기 | **32MB** | 충분히 여유 있고 호스트에서 mount/edit도 빠름. |
| 쓰기 정책 | **write-through** | 동기화 버그 최소화. 성능 문제 생기면 나중에 캐시 추가. |
| 호스트 측 편집 도구 | **자체 도구** (`tools/modern/bxos_fat.py`, `mkfat12.py` 확장) | 외부 의존 없이 macOS/Linux 동일 동작. |
| 데이터 디스크 기본 경로 | **`build/cmake/data.img`** | CMake 빌드 디렉터리와 동일. `run-qemu.sh` 가 자동 부착. |
| 게스트 드라이브 명명 | **`A:` = FDD(부팅), `C:` = HDD(데이터)**, 콘솔 기본 = `C:` | Phase 3 에서 도입. 기본 드라이브가 FDD → HDD 로 바뀌는 호환성 변경. |

## 3. 작업 단계

체크박스(☐)는 PR 단위로 끊을 수 있는 자연스러운 경계를 표시한다.

### Phase 0 — 준비 / 결정 확정 (완료 — 2026-04-28)
- ☑ 2장의 결정사항 확정 (위 표).
- ☑ [run-qemu.sh](run-qemu.sh) 에 데이터 디스크 인터페이스 추가:
  - `--data <path>` / `--data=<path>` — 명시 지정 (`-hda` 로 부착)
  - `--no-data` — 자동 부착 무시
  - `BXOS_DATA_IMG` 환경 변수
  - 기본 경로 `build/cmake/data.img` 가 존재하면 자동 부착
  - 부팅 이미지 기본 경로도 `build/cmake/haribote.img` → `harib27f/haribote.img` 폴백으로 정리
- ☑ 데이터 디스크 기본 경로 컨벤션: **`build/cmake/data.img`** 로 확정 (2장 표 반영).
- ☑ Phase 1 착수 및 완료.

### Phase 1 — 빌드 구조 분리 (완료 — 2026-04-28)
- ☑ [tools/modern/mkfat12.py](../tools/modern/mkfat12.py) 에 `--fs {fat12,fat16}` + `--size` 옵션 추가. `FsParams` 데이터클래스 도입으로 BPB/FAT 폭 분기 일원화. FAT12 회귀 OK.
- ☑ CMake 타겟 정리:
  - `kernel-img` : `build/cmake/haribote.img` (FAT12 1.44MB) — `HARIBOTE.SYS` + `NIHONGO.FNT` 만
  - `data-img`   : `build/cmake/data.img` (FAT16 32MB) — HE2 앱 23개 + 데모 데이터 8개
  - `image`, `bxos ALL` : 둘 다
  - `run` : `-fda haribote.img -hda data.img` 둘 다 부착
- ☑ `run-qemu.sh` 는 Phase 0 에서 이미 정비됨 (자동 부착 동작 확인).
- ☑ QEMU monitor `info block` 으로 `floppy0` + `ide0-hd0` 둘 다 인식 확인.
- ☑ 게스트는 아직 HDD 인식 못함 — 부팅 화면까지 도달하면 끝. 앱 실행은 Phase 3 이후.

### Phase 2 — ATA PIO 블록 드라이버 (완료 — 2026-04-28)
- ☑ [harib27f/haribote/ata.c](../harib27f/haribote/ata.c) 신설:
  - `ata_identify(drive, *out)` — IDENTIFY (0xEC), 모델명/섹터수 파싱
  - `ata_read_sectors(drive, lba, count, buf)` — 28-bit LBA PIO read (0x20)
  - `ata_write_sectors(drive, lba, count, buf)` — write (0x30) + cache flush (0xE7)
  - `ata_init()` — master/slave 모두 IDENTIFY 시도하고 결과를 `ata_drive_info[]` 에 캐시
  - 폴링 + 타임아웃, 인터럽트 미사용
- ☑ [bootpack.h](../harib27f/haribote/bootpack.h) — `struct ATA_INFO`, 함수 선언, `io_in16/out16` 선언 추가
- ☑ [bootpack.c](../harib27f/haribote/bootpack.c) `HariMain` 초기화에 `ata_init()` 호출 (PIC 초기화 직후)
- ☑ 콘솔 명령 `disk` 추가 ([console.c](../harib27f/haribote/console.c) `cmd_disk`):
  - 캐시된 IDENTIFY 결과(모델명/섹터수/크기) 표시
  - LBA 0 한 섹터 read 검증 → OEM ID + boot signature 표시
- ☑ CMake/Makefile.modern 의 `KERNEL_C_NAMES` 에 `ata` 등록
- ☑ QEMU 부팅 후 `disk` 명령 실행 → `QEMU HARDDISK / sectors=65536 / size=32MB / LBA0 oem='HARIBOTE' sig=aa55` 정상 출력 확인

### Phase 3 — FAT16 읽기 경로 통합 (완료 — 2026-04-28)
- ☑ [harib27f/haribote/fs_fat.c](../harib27f/haribote/fs_fat.c) 신설:
  - `struct FS_MOUNT` (BPB 파싱 결과 + FAT/루트 캐시)
  - `fs_mount_data(drive)` — BPB 한 섹터 read → FAT 32KB + 루트 16KB 캐시
  - `fs_data_search(name)` — 기존 file_search 위에 마운트된 루트 디렉터리 사용
  - `fs_data_loadfile(clustno, *psize)` — FAT16/FAT12 체인 추적 + ATA cluster read + tek 압축 풀기
- ☑ 기존 callsite 교체 ([console.c](../harib27f/haribote/console.c)):
  - `cmd_dir` → `fs_data_root()` / `fs_data_root_max()` 사용
  - `app_find` 의 file_search 3곳 → `fs_data_search`
  - `app_subsystem` / `cmd_app` 의 file_loadfile2 → `fs_data_loadfile`
  - syscall `api_fopen` (edx=21) 의 file_search/file_loadfile2 → 같이 교체
- ☑ [bootpack.c](../harib27f/haribote/bootpack.c) `HariMain`: memman 초기화 직후 `fs_mount_data(0)` 호출 (master = data.img)
- ☑ 부팅 FDD nihongo.fnt 로딩은 그대로 유지 (IPL 이 메모리 로드한 FAT12 이미지 사용)
- ☑ CMake/Makefile.modern 의 `KERNEL_C_NAMES` 에 `fs_fat` 등록
- ☑ 검증 (QEMU 화면 캡처):
  - `dir` → data.img 의 28개 파일(.he2 20 + 데모 데이터 8) 정상 표시
  - `winhelo` (974B, 1 cluster) → "hello" 창 정상 실행
  - `tetris` (4488B, 3 cluster) → 게임 보드 + SCORE/LEVEL/NEXT 정상 그려짐 (멀티-클러스터 체인 OK)
  - `type euc.txt` (syscall fopen 경로) → 파일 내용 정확히 출력

### Phase 4 — FAT16 쓰기 경로 (완료 — 2026-04-28)
**목표**: 파일 생성/추가/삭제. 이번 작업의 핵심.

- ☑ `fs_fat.c` 에 다음 추가:
  - `fs_data_create(name)` — 빈 루트 디렉터리 엔트리 생성. 8.3 이름 대문자 변환.
  - `fs_data_write(finfo, pos, buf, n)` — 클러스터 부족 시 FAT16 체인 확장, 부분 클러스터 read-modify-write, 파일 크기 갱신.
  - `fs_data_truncate(finfo, size)` — 현재 크기 이하로 축소, 불필요한 FAT 체인 해제.
  - `fs_data_unlink(finfo)` — 루트 엔트리 삭제 마크(0xE5) + FAT 체인 해제.
  - `fs_mkdir(path)` / `fs_rmdir(path)` 는 선택 항목이라 이번 phase 에서는 보류.
- ☑ FAT 갱신은 **두 카피 모두 일관**되게 쓰기 (FAT1/FAT2 동기화).
- ☑ Write-through: 데이터 클러스터, FAT 섹터, 루트 디렉터리 섹터 변경을 즉시 ATA로 flush.
- ☑ 검증용 콘솔 명령 추가:
  - `touch <file>` — 빈 파일 생성.
  - `rm <file>` — 파일 삭제.
  - `echo <text> > <file>` — 텍스트 파일 덮어쓰기.
  - `mkfile <file> <bytes>` — A-Z 패턴으로 큰 파일 생성.
- ☑ 단위 테스트 대신 **시나리오 검증**:
  - QEMU HMP `sendkey` 로 임시 HDD 이미지에 `echo hello > echo.txt` 실행 → 호스트 FAT 파싱 결과 `ECHO.TXT` 내용 `hello\n`, `fsck_msdos -n` 통과.
  - `mkfile big.bin 102400` 실행 → `BIG.BIN` 크기 102400B, FAT16 체인 50 clusters, `fsck_msdos -n` 통과.
  - `mkfile test.txt 20` 후 `rm test.txt` 실행 → 파일 엔트리 제거, free cluster 수 원복, `fsck_msdos -n` 통과.

### Phase 5 — 사용자 공간 API 확장 (완료 — 2026-04-28)
**목표**: 앱이 파일을 쓸 수 있게 한다.

- ☑ [apilib](harib27f/apilib) 의 `api_fopen` 류 함수 정리/확장:
  - `api_fopen_w(path)` — 쓰기 모드로 파일 생성/덮어쓰기.
  - `api_fwrite(fh, buf, n)` — 현재 파일 위치에 raw bytes write.
  - `api_fdelete(path)` — 파일 삭제.
- ☑ syscall 디스패처([console.c](harib27f/haribote/console.c) `hrb_api`) 에 신규 번호 추가:
  - edx=28 `api_fopen_w`
  - edx=29 `api_fwrite`
  - edx=30 `api_fdelete`
  - `struct FILEHANDLE` 을 `mode`/`finfo` 포함 형태로 확장해 read buffer handle 과 write-through handle 을 같은 슬롯에서 관리.
- ☑ HE2 `libbxos` ([he2/libbxos](he2/libbxos))에도 동일 API wrapper 추가해서 modern 빌드 앱도 사용 가능하게 함.
- ☑ legacy HRB `apilib` 에 `api028.nas` / `api029.nas` / `api030.nas` 추가.
- ☑ 검증용 작은 앱 추가:
  - `harib27f/touch/touch.c` — `touch.he2 <file>`.
  - `harib27f/echo/echo.c` — `echo.he2 <text> > <file>`.
  - `harib27f/fdel/fdel.c` — `fdel.he2 <file>`.
- ☑ 검증:
  - 전체 빌드 성공, `data.img` 에 HE2 앱 23개 + 데모 데이터 8개 포함.
  - QEMU HMP `sendkey` 로 `echo.he2 hello > api.txt` 실행 → 호스트 FAT 파싱 결과 `API.TXT = hello\n`, `fsck_msdos -n` 통과.
  - `touch.he2 empty.txt` 실행 → 0바이트 파일 생성 확인.
  - `fdel.he2 api.txt` 실행 → 파일 삭제 및 free cluster 수 증가, `fsck_msdos -n` 통과.

### Phase 6 — 콘솔 명령 보강 (완료 — 2026-04-28)
- ☑ `cp <src> <dst>`, `rm <file>`, `mv <src> <dst>` 추가/검증.
  - `cp` 는 tek 자동 해제를 타지 않도록 `fs_data_read()` raw byte read 경로를 새로 사용.
  - `mv` 는 raw copy 후 원본 unlink 방식.
  - `rm` 은 Phase 4 의 `fs_data_unlink()` 기반 명령을 유지.
- ☑ `mkdir <dir>` 은 선택 항목이라 보류. 서브디렉터리 cluster 탐색/write path 가 필요해 Phase 6 범위 밖.
- ☑ 일반 출력 리다이렉션 `cmd > file` 은 보류. 현재는 built-in `echo <text> > <file>` 과 `echo.he2` 검증 앱만 지원.
- ☑ [BXOS-COMMANDS.md](BXOS-COMMANDS.md) 업데이트.
- ☑ 검증:
  - QEMU HMP `sendkey` 로 `cp a.he2 b.he2`, `rm a.he2`, `b` 실행 → `B.HE2` 가 원본 `A.HE2` 와 byte-for-byte 동일, `A.HE2` 삭제 확인, `fsck_msdos -n` 통과.
  - `mv type.he2 t2.he2` 실행 → `T2.HE2` 가 원본 `TYPE.HE2` 와 byte-for-byte 동일, `TYPE.HE2` 삭제 확인, `fsck_msdos -n` 통과.

### Phase 7 — 호스트 측 디스크 이미지 편집 도구 (완료 — 2026-04-28)
**목표**: 앱을 다시 빌드했을 때, **전체 데이터 이미지를 재생성하지 않고도** 호스트에서 파일을 디스크 이미지에 넣고 빼기.

- ☑ `tools/modern/bxos_fat.py` 추가:
  - `bxos_fat.py create   data.img --size 32M`
  - `bxos_fat.py cp HOST:hello.he2 data.img:/hello.he2`
  - `bxos_fat.py rm data.img:/hello.he2`
  - `bxos_fat.py ls data.img:/`
  - 추가로 검증 편의를 위해 `data.img:/file HOST:file` 추출과 같은 이미지 내부 복사도 지원.
- ☑ CMake 에 `install-<APP>` 헬퍼 타겟 추가:
  - `cmake --build build/cmake --target install-tetris` → 빌드된 `tetris.he2` 를 `data.img` 에 복사.
  - `data-img` 전체 재생성을 의존하지 않고, 앱 산출물만 빌드한 뒤 기존 `data.img` 를 부분 갱신.
- ☑ macOS/Linux에서 동일하게 돌도록 외부 의존 없는 순수 Python 유지.
- ☑ 검증:
  - `/tmp/bxos-fat-tool.img` 생성 → host `tetris.he2` 복사 → 이미지에서 다시 추출 → `cmp` 일치.
  - `bxos_fat.py rm` 후 `fsck_msdos -n` 통과.
  - `cmake --build build/cmake --target install-tetris` 성공.
  - `data.img:/tetris.he2` 추출본이 `build/cmake/he2/bin/tetris.he2` 와 byte-for-byte 동일, `fsck_msdos -n build/cmake/data.img` 통과.

### Phase 8 — 문서화 / 마무리 (0.5일)
- ☐ [_doc](_doc) 에 **드라이브 모델 / FAT16 레이아웃 / ATA 사용** 설명 문서 추가.
- ☐ [README.utf8.md](README.utf8.md) / [SETUP-MAC.md](SETUP-MAC.md) 의 빌드/실행 섹션 갱신 (이미지 두 개 구조).
- ☐ [BXOS-COMMANDS.md](BXOS-COMMANDS.md) 에 신규 콘솔 명령 추가.

## 4. 마일스톤 / 검증 시나리오

각 페이즈가 끝났을 때 다음이 동작해야 한다:

| 끝난 시점 | 검증 |
|---|---|
| Phase 1 | `kernel-img` 만 다시 빌드해도 부팅됨. `data-img` 빌드는 별도. |
| Phase 2 | 부팅 시 디버그 창에 IDENTIFY 결과(모델/섹터수) 출력. |
| Phase 3 | `dir` 명령이 HDD(`C:`) 의 내용을 보여주고, 거기 들어 있는 `winhelo` 가 실행됨. |
| Phase 4 | 콘솔에서 파일 생성 → 재부팅 후에도 그 파일이 살아있음. |
| Phase 5 | 사용자 앱(`echo.he2 hi > x.txt`)이 동작. |
| Phase 6 | `cp a.he2 b.he2 && rm a.he2 && b` 시퀀스 동작. |
| Phase 7 | 호스트에서 `bxos_fat.py cp build/.../tetris.he2 data.img:/tetris.he2` 한 번으로 새 앱 반영, QEMU 재시작 후 `tetris` 실행 가능. |

## 5. 위험 요소 / 함정

- **ATA PIO 폴링은 기준 클럭 따라 hang 가능** — 타임아웃 + 디버그 창 로깅 필수.
- **FAT 쓰기 일관성** — 디렉터리 엔트리 갱신과 FAT 갱신 순서가 깨지면 부팅 후 디스크가 깨진 채 보임. 한 트랜잭션처럼 묶어서 작성.
- **파일명 8.3 한계** — 기존 mkfat12.py 와 동일하게 **단순 절단 정책 유지**. LFN은 이번 작업 범위 밖.
- **부팅 디스크와 데이터 디스크 동시 마운트** — 경로 prefix 없는 기존 명령들의 기본 드라이브를 명확히 정의해두지 않으면 사용자 혼란.
- **하위 호환** — 기존 단일 이미지 빌드(`build-modern.sh`)를 당분간 유지할지, 깔끔하게 폐기할지 Phase 1에서 결정.

## 6. 범위 외 (이번 작업에서 안 하는 것)

- LFN(긴 파일명) 지원
- ext2 등 다른 파일시스템
- 디스크 캐시/버퍼 풀
- 멀티 유저 / 권한
- 부팅을 HDD에서 직접 (MBR/파티션 테이블 없이 raw FAT16 사용 가정)
- 파일 잠금 / 동시 쓰기 보호 (단일 콘솔 환경이라 일단 무시)

## 7. 예상 일정

총 **10~13 작업일** (한 사람 풀타임 기준). Phase 2~4 가 가장 무겁고, Phase 7 의 호스트 도구는 Phase 4와 병행 가능.
