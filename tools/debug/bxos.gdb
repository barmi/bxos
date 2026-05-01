# tools/debug/bxos.gdb
#
# BxOS 커널 + HE2 앱 디버깅용 GDB 헬퍼.
#
# 사용법:
#   1. 별도 터미널에서 GDB stub 모드로 QEMU 실행:
#        ./run-qemu.sh --debug         # CPU halt + tcp:1234
#   2. i686-elf-gdb 시작하고 이 스크립트를 source:
#        i686-elf-gdb -x tools/debug/bxos.gdb build/cmake-debug/bootpack.elf
#
# CLion 의 "GDB Remote Debug" run config 에서는 .gdbinit 또는 "Initial commands"
# 에 다음만 넣으면 됩니다:
#        source /절대경로/bxos/tools/debug/bxos.gdb
#        bxos-attach
#
# Debug build 가 이미 만들어져 있어야 DWARF 심볼이 들어 있습니다:
#        cmake -S . -B build/cmake-debug -DCMAKE_BUILD_TYPE=Debug
#        cmake --build build/cmake-debug

set confirm off
set pagination off
set print pretty on
set disassembly-flavor intel

# x86 32-bit, real-mode 부팅 부분도 다루기 위해 i8086/i386 스위칭을 손쉽게.
set architecture i386

# 커널은 ELF 가 .text@VMA 0 / .data@VMA 0x310000 으로 링크되어 있고,
# asmhead 가 .text 를 0x00280000 으로 복사한다 (CS base = 0x00280000).
# .data 는 ELF VMA 그대로 0x00310000 에 적재된다.
#
# bxos-attach 는 위 사실을 바탕으로 bootpack.elf 의 .text 를 0x00280020
# 위치(시작점 _HariStartup)에 매핑하고 data 섹션 주소도 손봐 둔다.
# QEMU stub 에 연결만 하고 싶을 때 사용.
define bxos-attach
  target remote localhost:1234
  # bootpack.elf 의 .text 는 ELF 파일에서 0x00 부터 시작하지만, 실제 메모리에는
  # 0x00280000 + (32B HRB 헤더 0x20) 위치에 적재된다.
  # 32B 헤더 자체도 .text.startup 의 일부라 .text 시작은 0x00280000 으로 둔다.
  if $argc == 1
    set $kernel_elf = $arg0
  else
    set $kernel_elf = "build/cmake-debug/bootpack.elf"
  end
  printf "[bxos] connected. kernel ELF = %s\n", $kernel_elf
  printf "[bxos] tip:  break HariMain  /  break console_task  /  c\n"
end

# 이미 attach 된 상태에서 기본 break point 들을 깐다.
define bxos-breakpoints
  rbreak HariMain
  rbreak console_task
  rbreak start_menu_dispatch
  rbreak fs_data_open_path
  rbreak hrb_api
  info breakpoints
end

# HE2 앱을 디버그할 때, 앱이 메모리에 올라간 base address 를 받아 심볼을 add.
# 호출 예: bxos-load-app build/cmake-debug/he2/obj/explorer/explorer.elf 0x12345000
# base 는 보통 'p task->tss.cs' 등으로 얻은 segment base 와 같다.
define bxos-load-app
  if $argc < 2
    printf "사용법: bxos-load-app <app.elf> <load_base_addr>\n"
  else
    add-symbol-file $arg0 $arg1
    printf "[bxos] app symbols loaded at 0x%x\n", $arg1
  end
end

# QEMU 가 처음 멈춰 있을 때(real-mode IPL) 실행. asmhead 가 protected mode 로
# 진입한 직후 bootpack 의 _HariStartup 으로 jump 하는 위치에 break point 를 깐다.
# protected mode 진입 후에는 자동으로 i386 모드를 다시 set.
define bxos-boot
  set architecture i8086
  printf "[bxos] real mode (i8086). break at protected-mode entry...\n"
  # asmhead.nas 의 'JMP DWORD 2*8:0x0000001b' 이후 첫 명령 = 0x00280020
  hbreak *0x00280020
  c
  set architecture i386
  printf "[bxos] entered protected mode. _HariStartup at 0x00280020.\n"
end

printf "[bxos] gdb helpers loaded. commands:\n"
printf "         bxos-attach [<bootpack.elf>]    — connect to QEMU :1234\n"
printf "         bxos-breakpoints                — drop common breakpoints\n"
printf "         bxos-boot                       — break at protected-mode entry\n"
printf "         bxos-load-app <elf> <base>      — load HE2 app symbols\n"
