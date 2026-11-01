# Create an X6 Reloaded SD card on Windows

For the complete path from an original unit to a playable card, start with
[the setup guide](SETUP.md). Take your FEL backup on a Mac/Linux host first;
this page covers the proposed WSL build and Windows image-writing stages.

Use **balenaEtcher** or **Raspberry Pi Imager** to write your private
`X6-Reloaded.img` to a spare SD card. Both have Windows versions and verify the
write. No WSL, Make, Python or compiler is needed for the actual card write.

The image contains an MBR and a FAT32 partition beginning at 1 MiB. Windows can
read that partition normally. The SD boot stage lives before the partition, so
formatting a card and copying the image's visible files is insufficient. Write
the entire image. Do not format it as exFAT, NTFS or GPT.

**Writing an image erases the selected card, including saved settings and games.**
Use a spare card and back up anything you need first. This procedure writes the
SD card only. It does not update the X6's internal flash.

## Get your private image

Use `dist/X6-Reloaded.img` and `dist/X6-Reloaded.img.sha256` from your own build.
You can copy these two files from your Mac to Windows, or build them using the
WSL route below. Keep both files together on the computer, outside the target SD
card. No public image is supplied: generated images contain your firmware-derived
boot files and selected ROMs. Keep them private.

If Python 3.11+ is installed on Windows, open PowerShell in this repository and
verify the image before writing:

```powershell
py -3.11 tools/verify_image.py
```

For an image copied elsewhere (including paths containing spaces):

```powershell
py -3.11 tools/verify_image.py "C:\X6 builds\X6-Reloaded.img"
```

The verifier reads only regular local files. It checks the image against its
SHA-256 sidecar and checks the MBR/FAT32 envelope. It does not access the card or
prove that the image boots. Stop on failure; rebuild or recopy both files.

Without Python, PowerShell can compare the normal build's checksum directly:

```powershell
$expected = ((Get-Content -LiteralPath .\dist\X6-Reloaded.img.sha256 -Raw).Trim() -split '\s+')[0]
$actual = (Get-FileHash -LiteralPath .\dist\X6-Reloaded.img -Algorithm SHA256).Hash
if ($expected -notmatch '^[0-9a-fA-F]{64}$' -or $actual -ine $expected) { throw 'Image checksum mismatch' }
Write-Output 'Image checksum matches'
```

This comparison detects a damaged copy; it is not a substitute for the imager's
card readback verification.

## Write the card on Windows

### Method 1: balenaEtcher

1. Install [balenaEtcher for Windows](https://etcher.balena.io/) from its official site.
2. Insert the spare SD in a card reader. Select **Flash from file** and open your
   `X6-Reloaded.img`.
3. Select the spare card as the target. Check its capacity and identity before
   confirming; the whole selected drive will be erased.
4. Select **Flash** and approve the Windows administrator prompt. Wait for both
   writing and validation to finish successfully.
5. Safely eject the card if it remains mounted. Insert it in the powered-off X6,
   disconnect USB, and power on.

### Method 2: Raspberry Pi Imager

1. Install [Raspberry Pi Imager for Windows](https://www.raspberrypi.com/software/).
2. If asked for a Raspberry Pi device, select **No filtering**. Choose **Use
   Custom** in the OS/image selection and open `X6-Reloaded.img`.
3. Select the spare SD card. Keep system-drive exclusion enabled and confirm the
   target capacity. Skip OS customisation if offered; do not apply Pi settings.
4. Review the target and confirm the erase/write. Allow verification to complete;
   do not select **Skip verification**.
5. Eject on completion, insert the card into the powered-off X6, disconnect USB,
   and power on.

The [official Imager instructions](https://www.raspberrypi.com/documentation/computers/getting-started.html#install-using-imager)
describe custom images, target selection and verification. Labels vary by version.

## Build the image on Windows using WSL2

This is the proposed source-build route for Windows. **A full build in WSL and a
Windows-written card have not yet been validated for this project.** Native
PowerShell compilation is not supported; the build expects Unix tool names and
paths. An existing private image from a validated macOS build can use either
Windows writer above without using WSL.

1. Install Ubuntu under WSL from an administrator PowerShell:

   ```powershell
   wsl --install -d Ubuntu-24.04
   ```

   Restart if requested, launch Ubuntu and create its user account. See
   [Microsoft's WSL instructions](https://learn.microsoft.com/en-us/windows/wsl/install).

2. In Ubuntu, install the build utilities and clone into the Linux home folder:

   ```sh
   sudo apt update
   sudo apt install python3 git make clang lld xz-utils
   git clone https://github.com/mchineboy/X6-Reloaded.git ~/X6-Reloaded
   cd ~/X6-Reloaded
   python3 --version
   ```

   Python must be 3.11+. Keep the checkout under `~/`, rather than building on
   `/mnt/c`, so Linux file semantics and build performance are preserved.

3. Download and extract **Arm GNU Toolchain 13.3.rel1**, the **Linux host** package
   for your WSL architecture, from [Arm](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads).
   On x64 Windows use `arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi.tar.xz`.
   Verify the archive against Arm's published checksum. Extract it under
   `~/toolchains/`; use its extracted root as `TOOLCHAIN`. Do not install the
   Windows `.exe` tools into WSL or substitute Ubuntu's different GCC release:
   the module linker currently expects the `13.3.1` library directory.

4. Configure your own files and build, using Linux paths. For example, files
   stored under `C:\X6 inputs` are available under `/mnt/c/X6 inputs`:

   ```sh
   make menuconfig \
     ROMS="/mnt/c/X6 inputs/roms" \
     FIRMWARE="/mnt/c/X6 inputs/internal-flash-backup.bin" \
     TOOLCHAIN="$HOME/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi"
   make image
   make verify-image
   ```

   These commands create ordinary local build files. They do not need access to
   the SD card. Keep the same firmware baseline and ROM requirements as the
   [main guide](README.md#what-you-bring).

5. Run `explorer.exe .` in the checkout to open it in Windows Explorer. Copy the
   `.img` and `.img.sha256` from `dist` to a local Windows folder, then use one of
   the Windows writers above. Do not run `make sdcard` inside WSL or attach a raw
   Windows disk to WSL for this procedure.

## After writing

Windows should expose the FAT32 volume containing `x6` and `game`. A larger card
will initially show the image's smaller partition with the rest unallocated;
automatic partition expansion is not implemented. Do not use Windows Format or
Disk Management to recreate the partition. Cancel unexpected format/repair
prompts and recheck the image and writer result before using the card.

Never rename the image or a flash backup to `ePDKv100.img`. That filename invokes
the stock internal-flash updater, which this project does not use.

## Validation status

As of November 1, 2026, the checksum/layout verifier and synthetic FAT32 image
tests run locally on macOS. A Windows CI job is included for those tests; it does
not test a real card or the full compiler toolchain. Windows CI execution, a WSL
source build, Windows filesystem mounting and each writer's X6 boot result
remain to be verified on Windows hardware.
