# Set up your own X6 Reloaded SD card

Start here with your original handheld and card. Finish with a verified spare
arcade SD, preserved backups, and the steps to launch your first game.
Allow **60–120 minutes for a first setup**, depending on downloads, compilation
and card speed. This is a planning estimate, not a measured build benchmark.

The reference computer is **Apple Silicon macOS**. Linux alternatives are shown
where needed. Windows users should read [the Windows build/write route](WINDOWS.md)
before starting: FEL backup currently needs a Mac/Linux host; native Windows
FEL access and an entire Windows/WSL build have not been validated here.

## Before you start

| Item | Requirement |
| --- | --- |
| Handheld | The supported Allwinner **F1C200S**, **8 MiB SPI NOR**, stock **V1.241017** board. The same case/name does not establish compatibility. |
| Original SD | Keep it intact. Its files and your internal-flash backup are separate backups. |
| Spare cards | Prefer two, at least **1 GB** each: label one **FEL recovery**, the other **Reloaded**. Keep the recovery card for later use. |
| Computer and connections | Card reader, USB **data** cable, internet for tools/source, and several GB of free local space. Charge the handheld first. |
| Your files | Your own compatible game ZIPs and optional sample ZIPs. This project supplies no ROMs or manufacturer firmware. |

If you have only one spare, you can reuse it for Reloaded **after** copying both
verified flash reads to another drive. Reimaging it removes its FEL entry helper;
keep the recovery image and source so you can recreate that card later.

**Every image write erases the selected SD card, including its existing files.**
Keep the original card out of the reader during image writes. This workflow
boots from SD and reads internal flash for backup; it does not flash new internal
firmware. Never rename a backup or image to `ePDKv100.img`.

## 1. Prepare your computer and source

1. **Install the host tools.** On macOS, install Apple's developer tools if they
   are absent:

   ```sh
   xcode-select --install
   ```

   Finish the installer before continuing; an “already installed” response is
   fine. Install [Homebrew using its official instructions](https://docs.brew.sh/Installation)
   if needed, then run:

   ```sh
   brew install python git make llvm pkg-config libusb
   ```

   On Ubuntu, use this instead:

   ```sh
   sudo apt update
   sudo apt install python3 git make clang lld pkg-config libusb-1.0-0-dev build-essential xz-utils rsync
   ```

2. **Clone this project into a local folder**, outside either SD card:

   ```sh
   git clone https://github.com/mchineboy/X6-Reloaded.git
   cd X6-Reloaded
   python3 --version
   ```

   Python must report **3.11 or later**. Run all later `make` commands from this
   `X6-Reloaded` directory, except those explicitly using `make -C`.
   If you already cloned the project, open that checkout instead of cloning over it.

3. **Select LLVM for this terminal.** On macOS:

   ```sh
   export CLANG="$(brew --prefix llvm)/bin/clang"
   export LD_LLD="$(brew --prefix llvm)/bin/ld.lld"
   ```

   On Linux:

   ```sh
   export CLANG=clang
   export LD_LLD=ld.lld
   ```

   These two settings apply to the current terminal. Repeat them if you open a
   new terminal. Check that both tools run:

   ```sh
   "$CLANG" --version
   "$LD_LLD" --version
   ```

4. **Download and extract Arm GNU Toolchain 13.3.rel1** from
   [Arm's official downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads).
   Choose the **arm-none-eabi** archive for your computer:

   | Computer | Archive |
   | --- | --- |
   | Apple Silicon Mac | `arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi.tar.xz` |
   | Intel Mac | `arm-gnu-toolchain-13.3.rel1-darwin-x86_64-arm-none-eabi.tar.xz` |
   | x64 Linux or x64 Windows using WSL | `arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi.tar.xz` |

   Verify the archive against Arm's published checksum and extract it to a
   permanent folder on your computer. The [release notes](https://documentation-service.arm.com/static/66bb3acb882fec713ef48f84)
   identify the host/target variants. Use the extracted root as `TOOLCHAIN`,
   not its `bin/` folder. Do not substitute `aarch64-none-elf`, `arm-none-linux-gnueabihf`
   or a different GCC release: the current linker expects the `13.3.1` libraries.

   Replace the example below with your extracted folder and check it:

   ```sh
   export TOOLCHAIN="/path/to/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi"
   "$TOOLCHAIN/bin/arm-none-eabi-gcc" --version
   ```

5. **Check the source tree** before preparing a card:

   ```sh
   make check-source
   ```

**Checkpoint:** Python, both LLVM tools and the ARM compiler run; the source check
passes. No card has been written.

## 2. Preserve the original system and take a flash backup

If you already have two matching full internal-flash reads from your own unit
and an original-card backup on another drive, skip to section 3. The build still
checks whether that firmware matches its supported baseline.

1. **Back up the original card's files.** Power off the handheld, remove the
   original SD and insert it into a reader. On macOS, replace the source below
   with that card's actual mounted folder; keep the destination as a new folder
   on the computer:

   ```sh
   mkdir -p local/original-card-backup
   rsync -a "/Volumes/YOUR_ORIGINAL_SD/" "$PWD/local/original-card-backup/"
   diff -qr "/Volumes/YOUR_ORIGINAL_SD/" "$PWD/local/original-card-backup/"
   ```

   This copies hidden files too. Require a successful copy and no differences
   reported by `diff`; stop and investigate read errors or differences. Linux
   users can use the same commands with their actual mounted source folder.
   Keep the directory structure, including `apps/` and `game/`. Safely eject the
   original card and store it. Copy the backup to another drive too. This file
   copy does not preserve raw boot sectors and does not back up internal flash.

2. **Build the USB reader and FEL image.** From the project root:

   ```sh
   mkdir -p local
   git clone https://github.com/xboot/xfel.git local/xfel
   git -C local/xfel checkout --detach 445e8aefe6914c85817cc9bd1d201629364b0ec6
   make -C local/xfel
   git clone https://github.com/linux-sunxi/sunxi-tools.git local/sunxi-tools
   make fel-image
   ```

   Reuse existing local source folders only after checking their state; do not
   delete them to repeat this guide. The output is **`dist/X6-FEL.img`**.
   It does not require a firmware dump and does not access a card.

3. **Write the card labeled FEL recovery.** On macOS:

   ```sh
   diskutil list
   make fel-sdcard DISK=/dev/diskN
   ```

   Replace `diskN` with the current **whole disk** for this spare card. Verify
   the displayed capacity/identity. The tool shows the required `sudo make`
   command; run that command only after checking the target. Enter the exact
   erase phrase it requests and wait for successful verification and eject.
   On Linux/Windows, use an image writer with readback verification; select
   **`X6-FEL.img`**, following the [recovery-card instructions](BACKUP.md#3-write-only-the-dedicated-recovery-sd).

4. **Boot into FEL and take the reads.** With the X6 off and USB disconnected,
   insert the FEL recovery SD. Power on, then connect the USB data cable to the
   computer. Connect only one FEL device. A black screen is possible; confirm
   entry using:

   ```sh
   make fel-check XFEL="$PWD/local/xfel/xfel"
   ```

   Continue only on a successful **`ID=0x00166300`** and **`8388608 bytes`** check:

   ```sh
   make backup XFEL="$PWD/local/xfel/xfel"
   ```

   On Linux, follow the [USB permission notes](BACKUP.md#1-prepare-the-computer)
   if access is denied. If FEL detection or either read fails, stop and use
   [the FEL troubleshooting steps](BACKUP.md#4-boot-the-handheld-into-fel).

5. **Save the verified backups and leave FEL.** Require **PASS: two matching
   8 MiB reads**. Copy the entire printed timestamped backup directory to another
   drive. Keep both reads, their logs and `verification.json`. Save the printed
   first-read filename for `FIRMWARE` below. Power off the X6, disconnect USB,
   and remove the FEL recovery card.

**Checkpoint:** the original card is stored safely, its files have a separate
backup, and two matching flash reads are preserved on another drive. Matching
reads are not proof of a tested internal-flash restore.

## 3. Choose your games and build the arcade image

1. **Prepare your ROM folder.** Keep your own game-name ZIPs on the computer,
   for example `dkong.zip`, `galaga.zip` and `digdug.zip`. Their contents must
   match the supported **MAME 2000 / 0.37b5** definitions. Keep ZIPs zipped.
   The checker can match renamed chips and repack compatible data; changing an
   archive's name cannot supply missing or incompatible chips.

2. **Add optional sample ZIPs** under the ROM folder's `samples/` directory,
   such as `samples/galaga.zip`. These are separate recorded effects; they are
   not ROM ZIPs. To use another sample directory, add `SAMPLES="/path/to/samples"`
   to the next command. Missing samples can leave effects silent.

3. **Configure the build using your actual paths.** `FIRMWARE` is one complete
   8 MiB flash read, not the original SD folder or the FEL image:

   ```sh
   make menuconfig \
     FIRMWARE="/path/to/backups/TIMESTAMP/internal-flash-read1.bin" \
     ROMS="/path/to/your/roms" \
     TOOLCHAIN="$TOOLCHAIN"
   ```

   The first run fetches the pinned core source into `local/mame2000/` and checks
   your firmware and ROMs. In the selector, numbers toggle games, `a` selects
   all available games and `s` saves. Only complete compatible games can be
   selected. Configuration is stored privately in `.config.json`.

   For a menu-only card, create an empty local ROM folder and use `GAMES=none`:

   ```sh
   mkdir -p local/roms
   make menuconfig FIRMWARE="/path/to/your/backup.bin" ROMS="$PWD/local/roms" TOOLCHAIN="$TOOLCHAIN" GAMES=none
   ```

   If no games are accepted, run `make scan ROMS="/path/to/your/roms"` and read
   the missing-chip/checksum reasons. Keep rejected firmware backups intact;
   this build accepts only the supported boot0, boot1 and ramdisk baseline.

4. **Optionally enable Original Arcade** using your original-card file backup:

   ```sh
   make menuconfig STOCK_BACKUP="/path/to/your/original-card-backup"
   ```

   Save the selection again. This importer currently requires the exact supported
   original `apps/mod/fba.mod` and `game/mame/2020bb.fba`; it seeds the stock
   library with 2020 Super Baseball. It is separate from MAME game selection.
   Skip this step for a MAME-only card. An unsupported stock backup does not
   prevent a MAME-only build; rerun configuration with `STOCK_BACKUP=none`.

5. **Build and check the private image.** No SD needs to be connected:

   ```sh
   make image
   make verify-image
   ```

   Require both commands to finish successfully. The outputs are
   **`dist/X6-Reloaded.img`** and **`dist/X6-Reloaded.img.sha256`**.
   `build/image-report.json` records the selected games, samples and firmware
   checksum. Keep all generated images private: they contain firmware-derived
   data and any selected games. An image checksum checks a local file; it does
   not establish successful card writing or handheld boot.

**Checkpoint:** the arcade image passes checksum/layout verification. The original
card, source ROMs and firmware reads are unchanged.

## 4. Write the Reloaded card and boot it

1. **Insert the card labeled Reloaded into a reader.** Keep the original and
   FEL recovery cards out of the reader. Back up any files already on this spare.
   Keep the `.img` and checksum on the computer, outside the target card.

2. **Identify the current target.** On macOS:

   ```sh
   diskutil list
   make sdcard DISK=/dev/diskN
   ```

   Replace `diskN` with the actual whole spare disk, not a partition such as
   `diskNs1`. Check the displayed media name, capacity and identity. Do not reuse
   the FEL card's old disk number: identifiers can change between insertions.

3. **Write only after reviewing the target.** Run the exact `sudo make sdcard`
   command printed by the writer and type its exact erase phrase. It rechecks
   identity after unmount, writes the image, verifies by full readback and ejects.
   Continue only when verification and eject succeed. If unmount or verification
   fails, stop; do not force the operation or boot a failed write.

   Windows users should instead follow [the Windows writer steps](WINDOWS.md#write-the-card-on-windows)
   with **`X6-Reloaded.img`**. Linux users can use a raw image writer with
   readback verification. `make sdcard` itself is macOS-only. Write the entire
   image; formatting a card and copying visible files does not install the SD
   boot stage. The resulting card uses MBR/FAT32, not exFAT or NTFS.

4. **Boot from the safely ejected card.** Turn the X6 fully off, disconnect USB,
   insert the Reloaded card and power on. Expect the X6 Reloaded startup marquee
   followed by the library menu. USB storage hands the card to the computer;
   leave USB disconnected while playing.

5. **Confirm the card's behavior.** Open **MAME Arcade** with A or Start. The
   selected games should appear; a `GAMES=none` build has no playable MAME games
   until you add them. If Original Arcade was enabled, its folder should also
   appear. If the unit instead stays in FEL, power off and check that you used
   `X6-Reloaded.img` and inserted the Reloaded card, not the recovery card.

**Checkpoint:** the Reloaded menu appears on your handheld. Host-side build success
alone does not establish this checkpoint.

## 5. Play, add games and keep your settings

1. **Launch a game** with A or Start. Press **Select** to insert a coin and
   **Start** for one player. Use the D-pad and B/A/Y for the first three actions.
   Physical volume buttons control volume. The supported-driver list does not
   promise equal speed or complete behavior in every game.

2. **Use Menu to pause.** The pause menu offers Resume and Back to Reloaded;
   B or Menu resumes. In the library, Select opens settings; B or Select saves
   and returns. Reloaded difficulty/rapid-fire settings apply to MAME games.

3. **Add supported games without rebuilding.** Power off, place the card in a
   reader and copy compatible game ZIPs into `roms/mame/`. Copy their sample ZIPs
   into `roms/mame/samples/`. For Original Arcade, use `roms/original/` and retain
   your original emulator at `x6/stock/fba.mod`. Safely eject, insert and reboot.
   Grey entries explain unsupported, invalid or unsuitable formats and cannot
   launch. See [runtime discovery and ROM requirements](README.md#add-games-without-rebuilding).

4. **Back up settings before any reimage.** Copy `x6/settings.bin` and per-game
   `.x6d` files beside your ROMs to a computer folder, along with any saves you
   need. Copy the whole card's files if you want to retain its other contents.
   Rebuilding a local image does not change the card; writing that image erases
   the card's previous games and settings. Larger cards initially retain the
   image's smaller partition; automatic expansion is not implemented.

5. **Return to stock when needed.** Power off, remove the Reloaded card and
   reinsert your preserved original card to use the original games. Boot with
   USB disconnected. Normal stock boot does not require flashing a backup.

## If a checkpoint fails

| Symptom | Next action |
| --- | --- |
| No FEL device or wrong flash capacity | Return to [FEL identification](BACKUP.md#4-boot-the-handheld-into-fel); check the recovery card, cable and board match. |
| Unsupported firmware / no compatible games | Keep your inputs; review `make scan` results and the stated firmware/ROM baseline before building again. |
| Compiler or linker missing | Repeat section 1's tool checks in the same terminal; `TOOLCHAIN` must point to the extracted 13.3.rel1 root. |
| Writer refuses the card / readback fails | Stop. Reidentify the actual removable whole disk and check the reader/card. Do not bypass target checks. |
| Stock menu, black screen or missing games after imaging | Disconnect USB; confirm the correct card/image and successful readback. For missing games check `roms/mame/` and the host ROM audit. Preserve the verified image and build report when reporting the result. |

**Validation status, November 1, 2026:** earlier SD-boot and emulator trials ran on
one X6. The new public workflow and FEL wrappers have host checks and cross-build
evidence; this complete first-time setup path still needs an end-to-end device
trial. Windows/WSL and Linux routes carry the additional limits described above.
