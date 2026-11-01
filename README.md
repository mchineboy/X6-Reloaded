# X6 Reloaded

More arcade. Less mystery meat menu.

X6 Reloaded boots a replacement arcade interface from an SD card on the tested
Allwinner F1C200S X6 handheld. Pick your games, build your card, power it on.
The menu, music, pause screen, difficulty settings and rapid fire are all there.
Now the thing even knows its own name.

**This is SD-card boot only. It does not overwrite the X6's internal firmware.**
The boot stage lives on the SD card; the firmware changes happen in RAM.
Power off and remove the card to return to the stock boot path. Keep your original
card somewhere sensible. Under a coffee mug does not count as a backup strategy.

## What you bring

| Input | What it needs to be |
| --- | --- |
| Handheld | The tested X6/F1C200S board with stock `V1.241017` firmware. An X6-shaped case is not a compatibility guarantee. |
| SD card | A spare card, at least 1 GB. The image writer **erases that card**. |
| ROMs | Your own self-contained game-name ZIPs. Contents must match the selected MAME 2000 / **0.37b5** definitions. |
| Firmware backup | Your own 8 MiB internal-flash backup from a compatible unit. Boot0, boot1 and ramdisk hashes must match the supported baseline. |
| Build tools | Python **3.11+**, Git, Make, Clang, LLVM `ld.lld`, and an extracted ARM GNU `arm-none-eabi` toolchain with newlib. The reference toolchain is **13.3.rel1**. |

ROMs and the firmware backup are separate inputs. There is no firmware download,
ROM downloader, bundled SDK, game artwork, or prebuilt emulator in this repository.
The firmware input is needed to generate the SD boot files locally. A folder full
of MAME ZIPs cannot supply the bootloader. Neither can enthusiasm. We checked.

The build and SD writer have been exercised on Apple Silicon macOS. The build
scripts avoid macOS-specific paths; Linux builds are not yet validated. The
included `make sdcard` writer is macOS-only.

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
| In a game | Select inserts a coin; Start starts one player; D-pad moves; B/A/Y are the first three arcade actions |
| Pause screen | Menu opens pause; B or Menu resumes; A selects; Left/Right changes difficulty or rapid fire where offered |
| Settings | Up/Down selects; Left/Right changes; B or Select saves and returns |
| Volume | The physical volume buttons |

Settings include music, clicks, brightness, screen blank and auto power off.
Global settings live in `x6/settings.bin`; per-game difficulty and rapid fire
live beside the ROMs as `.x6d` files. A fresh full SD image replaces those saved
settings along with the rest of the card. Back them up before rebuilding a card
you have already been using.

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
