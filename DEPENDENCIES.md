# Local dependencies

This repository does not vendor emulator cores, SDKs, vendor firmware, ROMs, or
recorded game samples. Do not add them to Git or publish generated images.

- MAME 2000 core: [libretro/mame2000-libretro](https://github.com/libretro/mame2000-libretro),
  pinned at `181b42e5da0d2f0c7a2e59244f23633ed594de2e`.
  `make deps` clones it into ignored `local/mame2000/`, preserving its upstream
  notices. `config/core-edits.json` records X6 edits without bundling the removed
  upstream code or unified-diff context. Read the upstream `readme.txt` and source
  notices before redistributing any resulting executable. Public source access
  is not blanket permission to redistribute its dependencies.
- ARM GNU toolchain: supplied locally by the user. The reference build uses
  `arm-none-eabi` 13.3.rel1, including its newlib headers/libraries and libgcc.
  Download it from [Arm's official toolchain page](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads).
  Its bundled notices and terms still apply; the toolchain is not included here.
- LLVM Clang and `ld.lld`: supplied locally by the user, under their upstream
  terms. No compiler binaries are included.
- X6 firmware: the user supplies a compatible backup. The build derives private
  SD boot data from it. Internal flash is read by the device's existing boot
  chain; the supplied backup is only read as an ordinary host file.
- ROMs and samples: supplied locally by the user. Repacking and matching checksums
  do not change their ownership or distribution terms.

No license is assigned here to third-party material. The UI pixel font, menu
music, startup drawing and integration tools are project source; their inclusion
does not grant rights to any game, brand, or vendor firmware.
