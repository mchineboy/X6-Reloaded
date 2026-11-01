# Dependencies, licenses and distribution

This repository does not vendor emulator cores, SDKs, vendor firmware, ROMs, or
recorded game samples. Do not add them to Git or publish generated images.

## Original project contributions

The [MIT License](LICENSE) applies to original X6 Reloaded contributions to the
extent the contributors hold the applicable rights. It does not replace upstream
licenses or grant rights to manufacturer firmware, ROMs, samples, third-party
artwork, or trademarks. Modified upstream code remains subject to its applicable
terms. Preserve all upstream copyright and license notices.

## Local dependencies

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

## MAME 2000 release review

The pinned core's [Usage and Distribution License](https://github.com/libretro/mame2000-libretro/blob/181b42e5da0d2f0c7a2e59244f23633ed594de2e/readme.txt)
is the older MAME license, not the license used by modern MAME. It restricts
commercial use and sale and requires freely available source for derivative
works. Individual components can carry separate terms. The project's MIT grant
does not make the combined emulator MIT-licensed or remove these conditions.

Before distributing a compiled emulator, review the exact source revision and
all linked components, retain their notices, and satisfy the applicable source
and distribution requirements. Publishing only integration source is not a
declaration that resulting binaries have been cleared for redistribution.

As of November 1, 2026, `src/arcade/frontend.c` enables
`mame2000-skip_disclaimer` and disables `mame2000-show_gameinfo`. The pinned
license's derivative-work section specifically restricts removing startup
information screens. The effect of these settings against that restriction
needs review before binary distribution; adding MIT does not resolve it.

## Firmware, ROMs and generated images

The image builder extracts and patches data from an owner-supplied firmware
backup. Its generated boot data and SD image contain vendor-derived bytes, even
when no games are selected. Optional stock-module imports, ROMs and recorded
samples retain their own ownership and distribution restrictions.

Keep these outputs private. Do not attach generated images, firmware, stock
modules, ROMs or samples to issues or releases. Ownership of a handheld or a
backup and successful checksum validation do not establish redistribution rights.
Any separate binary release needs its own provenance and license review.

## Compatibility research and affiliation

X6 Reloaded is independent and unofficial, with no affiliation or endorsement
from the handheld's manufacturer, Allwinner, or the MAME team. Product and game
names identify compatibility; no trademark rights are granted. Use original
project branding rather than manufacturer logos or claims of approval.

In the United States, [17 U.S.C. section 102(b)](https://www.law.cornell.edu/uscode/text/17/102)
distinguishes ideas, methods and discoveries from copyrightable expression.
[Section 1201(f)](https://www.law.cornell.edu/uscode/text/17/1201)
provides conditional exceptions for interoperability research and certain
related disclosures. Neither provision is blanket permission to copy firmware
or bypass access controls. This documentation does not establish that every
project activity qualifies for an exception. Other jurisdictions can differ.

A project license governs permissions to original contributions; it cannot
prevent objections or supply legal clearance for third-party rights. Preserve
provenance separating independently written code and interface observations
from copied implementation or assets, and review uncertain material before
publication.
