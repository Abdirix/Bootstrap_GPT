# write_gpt

A small C command-line tool that generates a raw GPT (GUID Partition Table) disk
image containing:

- A **protective MBR** (LBA 0), as required by the GPT spec
- A **primary and backup GPT header + partition table**
- An **EFI System Partition (ESP)** formatted as FAT32, with a
  `\EFI\BOOT\` directory ready to hold a UEFI bootloader
- A **Basic Data partition** for general-purpose storage

The resulting `.img` file can be booted directly in QEMU under UEFI (OVMF)
firmware, or written to a USB drive / disk for use on real UEFI hardware.

## Building

Requires a C compiler (`gcc`/`clang`) and a Unix-like environment.

```bash
gcc -std=c11 -Wall -Wextra -o write_gpt write_gpt.c
```

This produces a `write_gpt` binary in the current directory.

## Usage

```
Usage: write_gpt [options]
Options:
  -i,  --image <file>       Output disk image
  -es, --esp-size <size>    EFI System Partition size
  -ds, --data-size <size>   Basic Data partition size
  -b,  --bootloader <file>  UEFI bootloader to add
  -l,  --lba-size <bytes>   Logical block size
  -v,  --verbose            Print image layout
  -h,  --help                Show this help message
```

- `<size>` accepts a plain byte count or a suffixed value: `K`/`KB`,
  `M`/`MB`, `G`/`GB` (case-insensitive) — e.g. `40M`, `1G`, `512K`.
- If `--image` is omitted, the tool writes to `test.img` in the current
  directory by default.
- If `--bootloader` is omitted, the tool looks for `BOOTX64.EFI` in the
  current directory. If it isn't found, the image is still built, minus
  the bootloader (a warning is printed).

### Example

```bash
./write_gpt --image test.img --esp-size 40M --data-size 4M --bootloader BOOTX64.EFI --verbose
```

## Testing the image in QEMU

The generated disk is **GPT + UEFI only** — it has no legacy BIOS boot
code. That means you must boot it with **UEFI firmware (OVMF)**, not
QEMU's default SeaBIOS. Booting it without OVMF will just hang at
"Booting from Hard Disk".

### 1. Install OVMF

```bash
# Debian/Ubuntu
sudo apt install ovmf
# Fedora
sudo dnf install edk2-ovmf
# Arch
sudo pacman -S edk2-ovmf
```

### 2. Copy the writable VARS file

The `_VARS` file stores UEFI's boot variables and must be a local,
writable copy — not the read-only system template:

```bash
cp /usr/share/OVMF/OVMF_VARS_4M.fd ./OVMF_VARS_4M.fd
```

(Adjust the path if your distro places OVMF files elsewhere, e.g.
`/usr/share/edk2/ovmf/` on Fedora or `/usr/share/edk2-ovmf/x64/` on Arch.)

### 3. Boot the image

After compliation run the following command

```bash
./write_gpt \
--image test.img \
--esp-size 40M \
--data-size 2M \
--bootloader BOOTX64.EFI \
--verbose\
--run \
--ovmf-code /usr/share/OVMF/OVMF_CODE_4M.fd \
--ovmf-vars ./OVMF_VARS_4M.fd

```

Notes:

- The **code** pflash drive must come before the **vars** pflash drive —
  QEMU maps them to unit 0 and unit 1 respectively.
- `readonly=on` is required on the CODE image; it's meant to be
  read-only firmware.
- If OVMF boots successfully and finds `\EFI\BOOT\BOOTX64.EFI` on the
  ESP, it will hand off execution to your bootloader. If no bootloader
  was embedded, you'll land in the UEFI Shell instead — which is a good
  sign the disk itself is being recognized correctly.

## Project layout on disk

Running with `--verbose` prints the full layout (LBA ranges for the MBR,
GPT headers/tables, ESP, and data partition), which is useful for
sanity-checking a generated image or debugging with a hex editor.

## License