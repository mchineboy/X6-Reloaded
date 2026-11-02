# X6 Reloaded — alternative X6 handheld firmware (SD boot)

More arcade. Less mystery meat menu.

X6 Reloaded is an SD-boot alternative to the stock X6 handheld firmware interface,
with a custom arcade launcher built around MAME 2000. It targets the tested
Allwinner F1C200S X6 handheld. Pick your games, build your card, power it on.
The menu, music, pause screen, difficulty settings and rapid fire are all there.
Now the thing even knows its own name.

**This is SD-card boot only. It does not overwrite the X6's internal firmware.**
The boot stage lives on the SD card; the firmware changes happen in RAM.
Power off and remove the card to return to the stock boot path. Keep your original
card somewhere sensible. Under a coffee mug does not count as a backup strategy.

X6 Reloaded is an independent, unofficial project. It is not affiliated with or
endorsed by the handheld's manufacturer, Allwinner, or the MAME team.

## Start here: set up your own card

Follow [the start-to-finish setup guide](SETUP.md) for computer setup, original
card and FEL backups, game selection, image creation, verified SD writing and
your first game. Each stage has a checkpoint before you move on.

### Back up your unit

Follow [the step-by-step FEL and backup guide](BACKUP.md) before building your
arcade card. It starts with preparing a dedicated FEL recovery SD, explains how
to boot the X6 into FEL, and finishes with two verified internal-flash reads.

`make fel-image` creates the recovery image without needing a firmware backup.
Once the unit is in FEL, `make fel-check` checks its chip/flash identity and
`make backup` takes and compares two 8 MiB reads. Card writing is a separate,
explicitly confirmed step. These commands never program or erase internal flash.

## What you bring

| Input | What it needs to be |
| --- | --- |
| Handheld | The tested X6/F1C200S board with stock `V1.241017` firmware. An X6-shaped case is not a compatibility guarantee. |
| SD card | A spare card large enough for the generated image: at least 1 GB for a small arcade selection; 8 GB or more for the inspected full stock collection. The image writer **erases that card**. |
| ROMs | Your own self-contained game-name ZIPs. Contents must match the selected MAME 2000 / **0.37b5** definitions. |
| Firmware backup | Your own 8 MiB internal-flash backup from a compatible unit. Boot0, boot1 and ramdisk hashes must match the supported baseline. |
| Build tools | Python **3.11+**, Git, Make, Clang, LLVM `ld.lld`, and an extracted ARM GNU `arm-none-eabi` toolchain with newlib. The reference toolchain is **13.3.rel1**. |

ROMs and the firmware backup are separate inputs. There is no firmware download,
ROM downloader, bundled SDK, game artwork, or prebuilt emulator in this repository.
The firmware input is needed to generate the SD boot files locally. A folder full
of MAME ZIPs cannot supply the bootloader. Neither can enthusiasm. We checked.

**Windows card creation:** follow [the Windows guide](WINDOWS.md) to write your
private `.img` with Raspberry Pi Imager or balenaEtcher. The card uses MBR/FAT32,
so Windows can access its files after imaging. Copying files onto a freshly
formatted card does not install the SD boot stage.

The build and included `make sdcard` writer have been exercised on Apple Silicon
macOS. That writer is macOS-only. The Windows guide includes a WSL2 build route;
Linux/WSL builds and Windows card writes still need end-to-end validation.

## Build your card

1. Clone the source:

   ```sh
   git clone https://github.com/mchineboy/X6-Reloaded.git
   cd X6-Reloaded
   ```

2. Configure your local inputs and select games:

   ```sh
   make menuconfig \
     ROMS="/path/to/mame/roms" \
     FIRMWARE="/path/to/your-X6-internal-flash-backup.bin" \
     TOOLCHAIN="/path/to/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi"
   ```

   The first run fetches the pinned MAME 2000 source into ignored `local/` and
   applies the X6 changes there. It does not download games or firmware.
   `menuconfig` checks the ZIP contents and offers only complete, compatible games.
   Enter a game number to toggle it, `a` for all available games, and `s` to save.
   Paths and selections stay in ignored `.config.json`; later runs can simply use
   `make menuconfig`.

   For a script or a non-interactive terminal, append `GAMES=all` or
   `GAMES=dkong,galaga,digdug`. `make scan` lists the supported games and why an
   archive was accepted or rejected.

3. Build the SD image:

   ```sh
   make image
   ```

   The result is `dist/X6-Reloaded.img`, with a SHA-256 checksum beside it. The
   image contains the SD boot stage, replacement host, arcade module, and only
   your selected ROMs. Its FAT32 partition starts at 1 MiB, leaving room for the
   boot payload. Rebuilding starts with a fresh ROM staging folder, so deselected
   games do not come along for the ride.

   The engine always includes the full supported 43-driver profile. Your
   selection controls which ROMs are copied into the image, so adding another
   supported game later does not require rebuilding the engine.
   Configure with `GAMES=none` to build without ROMs and add them afterward.

   Runtime files live under `x6/`: `arcade.mod`, `x6menu.axf` and `boot.esh`.
   This keeps Reloaded separate from the stock `apps/mod/fba.mod` during
   file-based updates. The full-image writer below still erases its target card.

   ROMs are verified by size and CRC, then rebuilt under the filenames expected
   by 0.37b5. Your source ZIPs stay untouched. This can repack matching data and
   perform the narrowly checked Donkey Kong PROM conversion; it cannot invent
   missing chips or turn any random MAME version into the right one.

4. Identify and write the **spare SD card**:

   ```sh
   diskutil list
   make sdcard DISK=/dev/diskN
   ```

   Replace `diskN` with the actual whole SD disk shown by `diskutil`; do not copy
   somebody else's disk number. The first command displays the target identity.
   The writer asks you to rerun with `sudo` when needed, then requires an exact
   typed erase confirmation. It checks removable-media identity again after
   unmounting, writes the image, verifies the full image by readback, and ejects.
   **This replaces the selected SD card's partition table and contents. It does
   not write the handheld's internal flash.**

   You can also write the image with a disk imager that supports raw `.img` files.
   Choose the spare SD card, not a mounted folder or a partition. On larger cards,
   the initial image leaves the extra capacity unallocated.
   Windows users: use [Imager or Etcher](WINDOWS.md#write-the-card-on-windows)
   instead of the `diskutil` / `make sdcard` commands above.

5. Insert the card into the powered-off X6, disconnect USB, and power on.

   The original X6 Reloaded startup marquee appears briefly, then the game menu.
   USB storage exports the card to the computer; games cannot load while the
   computer has it. Charge first, arcade second.

## Samples and controls

Some games use external WAV samples. Put your own sample ZIPs under
`ROMS/samples/`, or supply a separate folder with
`SAMPLES="/path/to/samples"` when running `make menuconfig`.
The build copies the selected games' known sample files into the image. Missing
samples produce warnings; they can leave effects silent. Galaga's `bang.wav` and
`init.wav` are sample fallbacks, not a complete Namco 54xx emulation.

| Where | Control |
| --- | --- |
| Game list | D-pad to browse; A or Start to launch; Select for settings |
| Library folders | A or Start opens; B goes back; Left/Right pages within a list |
| In a game | Select inserts a coin; Start starts one player; D-pad moves; B/A/Y are the first three arcade actions |
| Pause screen | Menu opens pause; B or Menu resumes; A selects; Left/Right changes difficulty or rapid fire where offered |
| Settings | Up/Down selects; Left/Right changes; B or Select saves and returns |
| Volume | The physical volume buttons |

Settings include music, clicks, brightness, screen blank and auto power off.
Global settings live in `x6/settings.bin`; per-game difficulty and rapid fire
live beside the ROMs as `.x6d` files. A fresh full SD image replaces those saved
settings along with the rest of the card. Back them up before rebuilding a card
you have already been using.

The library opens with **MAME Arcade** and discovered stock libraries:
**Original Arcade, NES / Famicom, Super Nintendo, Mega Drive / Genesis,
Game Boy, Game Boy Color and Game Boy Advance**. A library with more than 40 games automatically
gets alphabet folders: **0–9, A–F, G–L, M–R, S–Z**. Empty groups are hidden;
punctuation and non-ASCII initial characters go under 0–9. Counts show how many
games each folder contains. Returning from a game restores its folder and cursor;
opening Settings also preserves your position. These are menu groups; no ROM
files are moved. Groups larger than 128 entries contain numbered game pages;
B returns through the page, alphabet and library folders. Returning from a
console restores its library, page and selected game. The host holds only sort
keys in memory and reads full metadata a page at a time from `x6/LIBRARY.IDX`.
The 8.3 cache filename and canonical backslash paths are used at the Melis file
boundary. The cache is rebuilt automatically at startup. A dedicated progress
screen shows the current phase/system, files checked, games found and a moving
activity indicator; no percentage is invented while the directory total is
unknown. Title loading, scanning, saving, sorting and opening the menu have
distinct labels. The total stock catalog is bounded at 8,192 entries and each source
directory at 4,096 directory entries. A failed scan shows the actual failed
operation/path and a diagnostic code in the menu; it is never used as an
in-progress message. Saves, instructions and hidden files are excluded.

## Add games without rebuilding

1. Copy your compatible ROM into the appropriate folder on the SD card:

   | Library | Folder | Files |
   | --- | --- | --- |
   | MAME Arcade | `roms/mame/` | Supported game-name ZIPs matching MAME 2000 / 0.37b5 |
   | Original Arcade | `roms/original/` | Compatible stock `.fba` packs or original-FBA game-name ZIPs |
   | NES / Famicom | `roms/nes/` | `.nes` |
   | Super Nintendo | `roms/snes/` | `.sfc`, `.smc` |
   | Mega Drive / Genesis | `roms/md/` | `.bin`, `.md`, `.gen`, `.smd` |
   | Game Boy / Color / Advance | `roms/gb/`, `roms/gbc/`, `roms/gba/` | `.gb`, `.gbc`, `.gba`, respectively |

2. Put any required MAME sample ZIP beside those ROMs under `roms/mame/samples/`.
3. Safely eject the card, insert it into the powered-off X6 and reboot.

The menu discovers installed games at startup and builds the alphabet folders
automatically. ROM bytes live on the card; they are not embedded in the binary.
MAME checks filenames for its compiled supported-driver pool and hides missing
ROMs. Extra ROM files remain visible in grey with an `UNSUPPORTED`, `INVALID` or
`FORMAT` badge. Selecting one shows the reason; neither A nor START launches it.
They participate in the same alphabet folders as playable entries. The scanner
considers `.zip`, `.fba`, `.7z`, `.rar`, `.bin` and `.rom`; instructions, saves,
hidden files, BIOS-only dependencies and subdirectories stay out of the game list.
Arbitrary games outside the driver pool still need an emulator update; copying a
ZIP cannot add an unimplemented hardware driver. `roms/mame/README.TXT` lists the
supported short names. The startup check does not validate every ROM chip CRC;
use `make scan ROMS=/path/to/roms` for the host-side audit.

Existing cards continue working: MAME also checks `game/cps/`, and Original Arcade
also enumerates packed games in `game/mame/` and the earlier `x6/stock/` trial folder.
The new `roms/` locations take priority when a game exists in both. Empty files
and invalid or unreadable original-arcade headers appear disabled. A recognizable
stock header or ZIP signature alone cannot establish whether the original FBA
engine supports that game: these entries remain eligible to try with a
`ROM set unverified` note. The startup scanner does not perform a complete
archive/ROM-set audit or promise that every selectable game will work.
Original Arcade needs your original module at `x6/stock/fba.mod`; it cannot run
stock games without that separate emulator. Original-FBA ZIPs are not assumed
interchangeable with MAME 0.37b5 ZIPs.

## Original arcade and console support

The initial stock-emulator trial used **2020 Super Baseball**. To include the
verified original modules and games from your own stock-card backup:

```sh
make image STOCK_BACKUP="/path/to/your/stock-card-backup"
```

This is optional. The importer checks the exact inspected `fba.mod`, `fc.mod`,
`sfc.mod`, `md.mod`, `gbc.mod` and `gba.mod` hashes before copying the supplied
modules into `x6/stock/`. Console modules may be absent; their games then show
`NO ENGINE` and cannot launch. The 2020 seed remains checksum-locked. Other stock
ROMs are copied under their existing `game/` folders and filenames; original
saves are not imported. The original GBA module's `res/` cache directory is also
created. Stock ROMs, modules, metadata and BIOS-derived bytes remain private
image outputs, never repository assets. The backup is read-only. It does not
convert `.fba` files into MAME ZIPs. Passing `STOCK_BACKUP` to `make menuconfig` saves the option
locally; use `STOCK_BACKUP=none` to disable it.

In the stock game, **Menu** opens Resume / Back to Reloaded. The host unloads one
emulator before loading the other because both use the same memory address.
Reloaded's per-game difficulty and rapid-fire settings apply to MAME games only.

Legacy console locations also work: `game/fc` and `game/nes`; `game/sfc` and
`game/smc`; `game/md` and `game/smd`; and `game/gb`, `game/gbc`, `game/gba`.
Recognized cartridge extensions route misplaced files to the correct console
library while retaining their physical paths. Two physical copies are listed separately; they can have different
saves or patches. Console archives are not assumed equivalent to raw cartridge
files. Add a compatible ROM, safely eject and reboot to rebuild the catalog.

### Readable titles without renaming files

The builder extracts an arcade ID/title/year/maker table from your original FBA
module into `x6/stock/arcade-titles.tsv`. `.fba` headers supply the ID, even when
the filename is misleading. SNES, Mega Drive, GB/GBC and GBA use readable internal
cartridge titles when present. These can still be abbreviated. NES uses a cleaned
filename unless a title index is supplied; the normal iNES header has no title.

`x6/titles.tsv` optionally overrides display names. Each line contains the
relative SD path, a TAB, and the display title. For example:

```text
game/nes/1114 ADVISL3.NES	Adventure Island 3 (USA)
```

Paths are case-insensitive and accept either slash style. Overrides never change
launch paths, filenames or save locations. To identify NES files automatically,
supply your own compatible DAT containing size, CRC32 and SHA1 records:

```sh
python3 tools/title_index.py --rom-root /path/to/stock-backup \
  --dat /path/to/nes.dat --output local/nes-titles.tsv
make image STOCK_BACKUP=/path/to/stock-backup TITLE_INDEX=local/nes-titles.tsv
```

The tool matches complete files or iNES cartridge payloads and skips ambiguous
or modified ROMs. It downloads no games or databases. Unmatched files retain
their filename fallback. The TSV supports up to 8,192 rows and 512 KiB; regenerate
or edit it after replacing ROMs. An oversized file causes a visible scan warning.

**Status, November 1, 2026:** the user reports that 2020 Super Baseball
“works perfectly” on the preceding trial. Native tests find 56 valid packed games
in the local stock backup. A native read-only backup audit discovers 5,164 stock
entries across seven libraries, including three misplaced files routed to their
correct consoles; a synthetic 6,003-entry NES test checks paging, source-index handoff, return and
closed handles. These are discovery/control-flow tests, not emulator execution.
Console modules share statically inspected lifecycle/interface layouts; their
playback, controls, sound, saves and return still need handheld acceptance.
The new directory syscalls, additional games and
automatic discovery still require handheld validation. Enumeration is not a
claim that every listed game runs correctly or fits the board's performance.

## What is actually tested

The preceding SD-boot host and arcade UI were run on one X6 on **November 1,
2026**. This release adds the X6 Reloaded name, original boot animation, and the
portable build/selection/image workflow. Those additions are checked on the host
and cross-compiled; **this release image has not yet been tested on hardware**.
The 43-driver profile is a supported build list, not a claim that every game is
perfect or equally fast. Altered Beast, in particular, asks a lot of this little
processor.

The public workflow checks the firmware components before patching a private
copy. Unknown firmware is rejected. It does not create or install an
`ePDKv100.img` update package. **Never rename a flash dump or the SD image to
`ePDKv100.img`**: the stock updater treats that filename as an internal-flash
update. This project does not use that path.

Returning from the replacement host to the stock launcher in the same boot is
still unreliable. To use the stock system, power off and remove this SD card.

## Development

```sh
make check       # Source audit, ROM/image/writer tests, sanitized UI tests
make preview     # Render UI previews into build/ui-preview/
make deps        # Prepare only the pinned local core source
```

`CLANG=/path/to/clang` and `LD_LLD=/path/to/ld.lld` override tool discovery.
`TOOLCHAIN` points to the extracted toolchain root, not its `bin/` folder.
An existing clean pinned source clone can be supplied with
`CORE_SOURCE="/path/to/mame2000-libretro"`; it is cloned privately rather than
edited in place. After that first preparation, builds need no network.

The public repository contains original integration/UI/tooling source and build
metadata. Upstream core source and toolchains are local dependencies, with their
own notices and terms; see [DEPENDENCIES.md](DEPENDENCIES.md). Firmware-derived
files, ROMs, samples, build outputs, local settings, and agent instruction files
are excluded. Do not upload your generated SD image to an issue or release.

## License and distribution

Original X6 Reloaded contributions are licensed under the [MIT License](LICENSE).
This covers the project's original integration code, UI, tooling, documentation,
pixel font, menu music and startup drawing, to the extent the contributors hold
the applicable rights. Third-party code and modifications to it remain subject
to the applicable upstream terms.

No rights to manufacturer firmware, game ROMs, recorded samples, third-party
artwork, or trademarks are granted. The generated SD image contains
firmware-derived data and any locally selected games or stock modules; keep it
private. Building from your own backup does not grant redistribution permission.

The combined MAME emulator is not MIT-licensed. The pinned MAME 2000 core uses
older terms with noncommercial and source-availability conditions, distinct from
modern MAME licensing. See [dependency licenses and release review](DEPENDENCIES.md)
before distributing any compiled component.

The project license grants permission to use original contributions; it does not
provide manufacturer approval or legal clearance for reverse engineering,
firmware modifications, or third-party redistribution.
