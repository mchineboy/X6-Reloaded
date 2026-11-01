# Back up your X6: FEL first, flash reads second

Start with a **dedicated spare SD card**, a card reader and a USB data cable.
Keep your original card out of the reader and out of the handheld during recovery
card preparation. The recovery card and the later arcade card serve different jobs.

This guide targets the inspected **Allwinner F1C200S / 8 MiB SPI NOR** board.
There is no verified button-only FEL shortcut for this unit. The tested entry
method boots a small SD helper that jumps to the chip's built-in USB recovery
mode. A black screen alone does not establish that FEL is running.

**Card imaging erases the selected spare SD. Internal flash is only read.**
XFEL uploads temporary code into RAM to detect/read SPI NOR; the commands below
do not program or erase internal flash. A successful backup does not prove that
a flash restore works. No restore command is provided.

## 1. Prepare the computer

Clone this repository and open a terminal in its root. You need Python 3.11+,
Git, Make, Clang with ARM support, LLVM `ld.lld`, and XFEL linked with libusb.
No ROMs, existing firmware dump or full ARM GNU toolchain are needed for this step.

On macOS, with Homebrew and Apple's command-line developer tools already installed:

```sh
brew install python git make llvm pkg-config libusb
```

On Ubuntu:

```sh
sudo apt update
sudo apt install python3 git make clang lld pkg-config libusb-1.0-0-dev build-essential
```

Build the pinned XFEL locally; a global install is unnecessary:

```sh
mkdir -p local
git clone https://github.com/xboot/xfel.git local/xfel
git -C local/xfel checkout --detach 445e8aefe6914c85817cc9bd1d201629364b0ec6
make -C local/xfel
```

If these directories already exist, inspect them before changing revisions; do
not remove an existing local checkout to repeat this guide. Keep XFEL's upstream
license and source beside its executable. For the commands below,
`XFEL="$PWD/local/xfel/xfel"` selects this exact executable.

The reference host is Apple Silicon macOS. Linux USB permissions may require
installing the pinned XFEL checkout's `99-xfel.rules` using your distribution's
udev procedure. Native Windows USB driver setup and WSL USB forwarding are not
validated here; use a Mac/Linux host for FEL access. The Windows image writer
can still prepare the recovery card in step 3.

## 2. Build the FEL recovery image

Fetch the helper source and generate an ordinary local image file:

```sh
git clone https://github.com/linux-sunxi/sunxi-tools.git local/sunxi-tools
make fel-image
```

The builder reads `fel-sdboot.S` from pinned upstream commit
`a08a24a2aa5804bde7fa6ea8cdb08135d018da33`, compiles for **ARMv5TE**, and checks
the complete 8,192-byte helper against the device-tested SHA-256:

```text
824367333824a5dfa3aef8c6386c5ea22d11df2d135c9525a835b387957ff85d
```

This architecture matters: do not substitute an arbitrary prebuilt ARMv7 helper.
The upstream source and notices remain in ignored local/build folders; no vendor
firmware is used. Override the source path with `SUNXI_TOOLS=/path/to/sunxi-tools`.
If linker discovery fails on macOS, use:

```sh
make fel-image LD_LLD="$(brew --prefix llvm)/bin/ld.lld"
```

Outputs are `dist/X6-FEL.img`, its `.sha256` sidecar, and
`build/fel/provenance.json`. The 512 MiB image has an MBR/FAT32 partition at 1 MiB
and the helper at byte 8,192. **This command does not access any SD card.**

## 3. Write only the dedicated recovery SD

On macOS, insert the spare card into a reader and identify its current whole disk:

```sh
diskutil list
make fel-sdcard DISK=/dev/diskN
```

Replace `diskN` with the actual spare card's disk identifier. Check the displayed
capacity and device identity. The writer will show the required `sudo make`
command; run it only after verifying the target. It requires an exact typed erase
confirmation, checks identity again after unmount, verifies by full readback and
ejects. Never reuse an old disk number. This step replaces the selected card's
partition table and contents.

On Windows or Linux, use a raw image writer with readback verification to write
**`X6-FEL.img`** to the spare. For Windows target-selection instructions see
[the card-writing guide](WINDOWS.md#write-the-card-on-windows); use the FEL image
in place of the arcade image, and follow the FEL boot sequence below. Do not
copy the image into a mounted folder. Cancel unexpected format/repair prompts.
`make sdcard` writes the arcade image; **`make fel-sdcard` writes the FEL image**.

## 4. Boot the handheld into FEL

1. Power the X6 fully off and disconnect its USB cable.
2. Remove the original/arcade card and insert the safely ejected FEL recovery SD.
3. Power on the handheld, then connect its USB data cable directly to the host.
4. Run the identity check from the repository root:

   ```sh
   make fel-check XFEL="$PWD/local/xfel/xfel"
   ```

5. Continue only if the check succeeds: chip **`ID=0x00166300`** and detected
   SPI NOR capacity **`8388608 bytes`**.

The display may stay black. In FEL the USB device is Allwinner **`1f3a:efe8`**,
not an SD storage volume. XFEL's chip ID identifies a family, not your unique
handheld; connect only one FEL device. On macOS, System Information → USB can
show the USB vendor/product IDs; on Linux use `lsusb`.

If XFEL finds no device, power off, check that the imager verified the correct
recovery card, try a known data cable/direct port, and repeat this section.
If the stock menu or a mounted SD storage volume appears, FEL entry has not been
established. If the chip/capacity differs, stop: this workflow is for the stated
board. Do not try flash writes or the stock firmware updater to fix detection.

## 5. Take, preserve and use the backup

Leave the unit connected in FEL and run:

```sh
make backup XFEL="$PWD/local/xfel/xfel"
```

The target repeats the chip/capacity check, creates a new timestamped private
directory under `local/backups/`, reads all 8 MiB twice, and requires identical
SHA-256 hashes. It checks actual file sizes rather than trusting XFEL's exit
status. Earlier backups are retained. Interrupted, short or mismatched reads
leave their files and a failure/incomplete record for diagnosis; they are not
reported as verified firmware.

After the command prints **PASS**:

1. Copy the entire timestamped directory to another drive. It contains both
   `internal-flash-read*.bin` files, read logs and `verification.json`.
2. Power off the X6, disconnect USB and remove the FEL card. Keep it labeled
   separately. Removing it restores the normal internal-flash boot path.
3. Back up the original SD's files separately using a card reader. Internal
   flash reads do not include the original card's games, modules or saved files.
4. Use the printed first-read path as the build input, for example:

   ```sh
   make menuconfig \
     FIRMWARE="/path/to/local/backups/TIMESTAMP/internal-flash-read1.bin" \
     ROMS="/path/to/roms" TOOLCHAIN="/path/to/arm-gnu-toolchain"
   ```

Matching reads establish consistency, not compatibility. The arcade image build
separately checks boot0, boot1 and ramdisk against its supported `V1.241017`
baseline. Keep a differing backup intact even if the build rejects it.
Continue with [game selection and image creation](SETUP.md#3-choose-your-games-and-build-the-arcade-image)
using an arcade spare.
Never rename any raw backup or SD image to `ePDKv100.img`.

## Evidence and upstream references

The earlier device session on October 31, 2026 verified FEL entry using the
exact helper hash above and obtained two matching 8 MiB reads from this board.
The newly generated full recovery-card image and these wrapper commands have
host checks; they still need an end-to-end device trial. Internal-flash restore
remains untested. Existing firmware dumps were not modified to develop this guide.

The helper's [pinned source and GPL notices](https://github.com/linux-sunxi/sunxi-tools/blob/a08a24a2aa5804bde7fa6ea8cdb08135d018da33/fel-sdboot.S)
describe the Boot ROM jump and SD placement. The pinned
[XFEL commands](https://github.com/xboot/xfel/blob/445e8aefe6914c85817cc9bd1d201629364b0ec6/README.md)
and [SPI NOR implementation](https://github.com/xboot/xfel/blob/445e8aefe6914c85817cc9bd1d201629364b0ec6/spinor.c)
provide detection and flash reads. Their licenses remain separate from this
project's original tooling; keep generated recovery artifacts local.
