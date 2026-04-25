# BxOS

* barmi (skshin@gmail.com, skshin@hs.ac.kr)
* 2012-09-11 처음 init

## 목표

* 운영체제 개념을 설명하기 위한 OS로 수정
* 필요한 기능을 단위별로 구현

## 할일

* 콘솔 스크롤
* 한글 출력
* 한글 입력기
* CPU 모니터

## 참여

* 어떤식으로든 참여할 사람은 연락바람

---

> 원본 `README` 파일은 EUC-KR로 저장되어 있습니다. 이 파일은 UTF-8로 변환된 사본입니다.
> 소스 트리(`harib27f/`)의 C 파일들은 EUC-KR/CP949 인코딩을 그대로 유지해야 빌드 결과(폰트/문자열) 가 정상 동작합니다 — 일괄 변환하지 마세요.

이 OS는 카와이 히데미의 *30일 OS 자작 입문* (Haribote OS, OSASK)을 기반으로 한국어 콘솔/입력기/CPU 모니터 등을 추가한 교육용 32-bit x86 OS입니다.

## 빠른 시작 (macOS Apple Silicon)

자세한 안내는 `SETUP-MAC.md` 참고.

```bash
# 1. QEMU 설치 (한 번만)
brew install qemu

# 2. 이미 빌드된 OS를 즉시 부팅
cd harib27f
qemu-system-i386 -m 32 -fda haribote.img -boot a
```

## 디렉터리 구조

| 경로 | 설명 |
|---|---|
| `harib27f/` | 최종 단계 OS 소스 + 부팅 가능한 `haribote.img`/`haribote.iso` |
| `harib27f/haribote/` | 커널(부트팩) 소스: bootpack.c, console.c, sheet.c, window.c … |
| `harib27f/<app>/`   | 사용자 모드 앱: invader, calc, bball, gview, mmlplay … |
| `z_tools/`           | 원본 Windows 빌드 툴체인 (.exe) — Wine 또는 현대 툴체인으로 대체 |
| `z_osabin/`, `z_new_o/`, `z_new_w/` | 보조 빌드 스크립트 |
