# ZeroTrace — Step-by-step conversion

**What this document is:** A polished, jury-ready Markdown walkthrough that documents exactly how the stock `ubuntu.iso` was modified and rebuilt into `ZeroTrace.iso`. It combines the actual terminal steps you ran with clear explanations, checks, troubleshooting, and reproducible commands.

---

## Summary

Goal: take an existing Ubuntu live ISO, inject custom binaries (`a.out`, `menu.sh`) and a login hook, then rebuild a fully-bootable ISO named **ZeroTrace.iso** that preserves UEFI (and optionally BIOS) boot.

This document explains each step, why it's required, how to verify success, and common fixes for errors you encountered in the terminal logs.

---

## Prerequisites / environment

* Host: Ubuntu (or Debian-based) environment with sudo access. WSL can work but testing is easier on a native Linux environment.
* Free disk space: at least **2×** the size of the ISO (recommended) because we unpack, edit and rebuild.
* Packages (install if missing):

```bash
sudo apt update
sudo apt install -y squashfs-tools xorriso genisoimage 7zip p7zip-full syslinux-utils grub-common ovmf
```

> Notes:
>
> * `squashfs-tools` is needed for `unsquashfs` / `mksquashfs`.
> * `xorriso` is used to create modern hybrid ISOs with UEFI and legacy options.
> * `genisoimage` provides compatibility for `mkisofs` style commands (you may see it installed as `mkisofs` alias).
> * `ovmf` is recommended to test UEFI boot in QEMU.

---

## Files you worked with (from your session)

* `ubuntu.iso` — original base ISO
* `zerotrace-base.iso` — base working ISO you used in some steps
* `a.out` — custom binary to include
* `menu.sh` — launcher script to include

---

## Directory layout used in the process (recommended)

We used / created these working directories in your logs:

```
~/iso_dir            # copied contents of the mounted ISO
~/mnt_dir            # temporary mount point
~/iso-check          # a copy of the ISO to prepare modifications
~/squashfs-root      # extracted squashfs root filesystem
~/zt-iso-work        # final working tree used to build new ISO
~/ZeroTrace.iso      # output ISO (target)
```

---

## Step 1 — Mount the original ISO (read-only)

Mount the stock ISO so you can copy its contents into a writable tree.

```bash
mkdir -p ~/mnt_dir ~/iso_dir
sudo mount -o loop ubuntu.iso ~/mnt_dir
# If mount fails to auto-setup loop device, try:
# sudo losetup -fP ubuntu.iso && sudo mount /dev/loopX ~/mnt_dir

# copy the files out to a writable folder
rsync -a ~/mnt_dir/ ~/iso_dir/
sudo umount ~/mnt_dir
```

**Why:** ISO images are read-only. We copy the files into `~/iso_dir` so we can edit and replace files.

**Common error in logs:** `mount: WARNING: source write-protected, mounted read-only.` — this is expected; you're mounting read-only. Use `rsync` to copy.

---

## Step 2 — Locate and identify the squashfs filesystem

Ubuntu live systems store the live root filesystem inside a squashfs under `casper/` or with names like `minimal.standard.live.squashfs`.

```bash
cd ~/iso_dir/casper
ls -la
# Example (from your logs): minimal.standard.live.squashfs
```

If there are multiple squashfs variants (language variants / minimal / standard), choose the one the ISO boots with — often `minimal.standard.live.squashfs` or `filesystem.squashfs`.

---

## Step 3 — Extract the squashfs to a writable directory

```bash
# create extraction target
mkdir -p ~/squashfs-root
# extract the squashfs into squashfs-root (use the exact file name you found)
sudo unsquashfs -d ~/squashfs-root ~/iso_dir/casper/minimal.standard.live.squashfs
```

**Notes from your logs:** `sudo unsquashfs minimal.standard.live.squashfs` produced `squashfs-root` and you later renamed it to `edit` using `sudo mv squashfs-root edit`.

**Why:** This gives a full live root filesystem you can edit.

---

## Step 4 — Make your modifications inside the extracted root

**Common edits you performed:**

1. Create `/usr/local/bin` inside the extracted root and copy your artifacts:

```bash
sudo mkdir -p ~/squashfs-root/usr/local/bin
sudo cp ~/a.out ~/squashfs-root/usr/local/bin/
sudo cp ~/menu.sh ~/squashfs-root/usr/local/bin/
sudo chmod +x ~/squashfs-root/usr/local/bin/a.out
sudo chmod +x ~/squashfs-root/usr/local/bin/menu.sh
```

2. Add a login hook so the menu or program runs automatically for live sessions. We placed a script in `/etc/profile.d/` so it runs for interactive shells/login shells:

```bash
sudo mkdir -p ~/squashfs-root/etc/profile.d
sudo tee ~/squashfs-root/etc/profile.d/zerotrace.sh > /dev/null <<'EOF'
#!/bin/sh
# zerotrace autostart for live session
# run the menu script only once per session
if [ -x /usr/local/bin/menu.sh ] && [ -z "$ZEROTRACE_STARTED" ]; then
  export ZEROTRACE_STARTED=1
  # run in background for graphical session
  /usr/local/bin/menu.sh &
fi
EOF
sudo chmod +x ~/squashfs-root/etc/profile.d/zerotrace.sh
```

**Why:** `/etc/profile.d` executes at login for shells; placing a script there makes the menu available at first login. (Adjust logic depending on whether your environment uses a display manager.)

**Security note:** If the ISO will be redistributed or used on Secure Boot systems, be aware that adding unsigned executables or changing the initramfs may break Secure Boot. For demonstration/test machines, disable Secure Boot or sign binaries.

---

## Step 5 — Prepare to re-generate the squashfs

Before packing, make sure any in-tree files that track the filesystem contents are updated (manifest and size):

1. Recompute `filesystem.size` (size in bytes):

```bash
# from inside your workspace
sudo du -sx --block-size=1 ~/squashfs-root | cut -f1 > ~/zt-iso-work/casper/filesystem.size
```

2. Recreate the manifest (recommended):

```bash
# generate a manifest listing installed packages and versions (if you want package lists to be correct)
sudo chroot ~/squashfs-root dpkg-query -W --showformat='${Package} ${Version}\n' > ~/zt-iso-work/casper/filesystem.manifest
```

> If `dpkg` is not available inside the chroot (rare), you can copy the old manifest, but regenerating keeps metadata correct.

---

## Step 6 — Repack the squashfs image

Make sure you build the new squashfs into a writable copy of the ISO tree (you cannot overwrite files on a mounted read-only mount). In the logs we created `~/zt-iso-work` as a writable copy of `~/iso-check`.

```bash
# create working copy of the iso tree (do not overwrite the mounted iso-check)
cp -rT ~/iso_dir ~/zt-iso-work
# then write the new squashed filesystem into the copied tree
sudo mksquashfs ~/squashfs-root ~/zt-iso-work/casper/minimal.standard.live.squashfs \
  -comp xz -b 1048576 -Xbcj x86 -noappend
```

**Options explained:**

* `-comp xz`: modern Ubuntu uses xz compression for squashfs.
* `-b 1048576`: block size tuned for performance on live images.
* `-Xbcj x86`: bcj filter for x86 binaries.
* `-noappend`: create a fresh squashfs instead of appending.

**Common error:** `Could not open regular file for writing as destination: Read-only file system` — that happens if you try to write into the original mounted ISO tree. Make sure you're rebuilding in a writable `~/zt-iso-work` directory.

---

## Step 7 — Update checksums inside the ISO tree

The `md5sum.txt` and other checksum files must reflect the new squashfs. A common approach is to regenerate the md5 list for the whole ISO tree before repacking the ISO.

```bash
cd ~/zt-iso-work
# remove any old md5sum.txt so it will be recreated
sudo rm -f md5sum.txt
# generate new list (skip boot catalog if present)
find . -type f -print0 | sudo xargs -0 md5sum | sed 's| ./| |' | sudo tee md5sum.txt
```

**Note:** Some Ubuntu ISOs expect `md5sum.txt` formatting and to exclude the `md5sum.txt` itself and boot catalog. Adjust the `find`/filtering as necessary.

---

## Step 8 — Rebuild the ISO (xorriso)

There are two common cases for the boot setup:

* **Legacy BIOS + UEFI (hybrid ISO)** — original ISO contains `isolinux/` and `isolinux.bin`.
* **UEFI-only** — modern Ubuntu desktop ISOs often use GRUB EFI without isolinux.

Check which your base ISO uses by listing the tree:

```bash
ls -la ~/zt-iso-work/isolinux || true
ls -la ~/zt-iso-work/boot/grub || true
ls -la ~/zt-iso-work/EFI || true
```

### A — If the ISO has `isolinux/` (BIOS) and `boot/grub/efi.img` (UEFI)

Use the hybrid xorriso command (this was used in your logs — adapt paths to what exists):

```bash
cd ~/zt-iso-work
sudo xorriso -as mkisofs \
  -r -V "ZeroTraceISO" \
  -o ~/ZeroTrace.iso \
  -J -l -cache-inodes \
  -isohybrid-mbr /usr/lib/ISOLINUX/isohdpfx.bin \
  -partition_offset 16 \
  -b isolinux/isolinux.bin \
    -c isolinux/boot.cat \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
  -eltorito-alt-boot \
  -e boot/grub/efi.img \
    -no-emul-boot -isohybrid-gpt-basdat \
  .
```

**Troubleshooting:**

* If xorriso complains `I cant find the boot catalog directory 'isolinux'` or `Cannot find ... isolinux.bin`, it means the copied tree lacks `isolinux`. If the original ISO was EFI-only, remove the `-b` / `-c` options and use the EFI-only command below.
* If `/usr/lib/ISOLINUX/isohdpfx.bin` is missing, install `syslinux-utils` or the `isolinux` package.

### B — If the ISO is EFI-only (no `isolinux` directory)

Use an EFI only xorriso invocation that registers the EFI image correctly. Usually Ubuntu includes `boot/grub/efi.img` or equivalent.

```bash
cd ~/zt-iso-work
sudo xorriso -as mkisofs \
  -r -V "ZeroTraceISO" \
  -o ~/ZeroTrace.iso \
  -J -l \
  -eltorito-alt-boot \
  -e boot/grub/efi.img \
    -no-emul-boot -isohybrid-gpt-basdat \
  .
```

**Why xorriso?** `xorriso` can create hybrid images suitable for both BIOS and UEFI and it can mimic the older `mkisofs` command via `-as mkisofs`.

> In your logs you ran a similar xorriso command and received an error complaining about missing isolinux boot files — that happened because the source tree used GRUB/EFI instead of isolinux.

---

## Step 9 — Verify the new ISO boots (testing)

### Quick QEMU test (BIOS):

```bash
qemu-system-x86_64 -cdrom ~/ZeroTrace.iso -m 2048 -boot d
```

### UEFI test (use OVMF firmware):

```bash
sudo apt install -y ovmf
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF_CODE.fd -cdrom ~/ZeroTrace.iso -m 2048
```

**What to check:**

* ISO boots to the live desktop or console
* Your `menu.sh` runs automatically (or your binary `a.out` is accessible and functional)
* The filesystem changes you made exist in the live environment (`ls /usr/local/bin`)

---

## Step 10 — Write ISO to USB (for demo on physical machine)

**WARNING:** `dd` will overwrite the device you choose. Double-check `/dev/sdX`.

```bash
# identify your USB device first
lsblk
# then, if the device is /dev/sdX (no partition number)
sudo dd if=~/ZeroTrace.iso of=/dev/sdX bs=4M status=progress oflag=sync
sync
```

Recommendation: use `balenaEtcher` or `Rufus` (Windows) if you want a GUI-based safe flashing tool.

---

## Troubleshooting — errors you hit and why

* `Command 'rsybc' not found` — typo; use `rsync`.
* `sudo: unumount: command not found` — typo; correct command is `umount`.
* `Could not open regular file for writing as destination: Read-only file system` while running `mksquashfs` — you were trying to write into a mounted read-only copy of the ISO. Build in a writable directory like `~/zt-iso-work`.
* `genisoimage: Uh oh, I cant find the boot catalog directory 'isolinux'!` — ISO uses UEFI/GRUB; you should not expect isolinux in modern Ubuntu desktop ISOs.
* `xorriso : FAILURE : Given path does not exist on disk: -boot_image system_area='/usr/lib/ISOLINUX/isohdpfx.bin'` — either install the syslinux utilities or use an EFI-only build if isolinux is not present.

---

## Quick reproducible script (example)

> **Use with care** — modify paths for your files and adjust names of squashfs if your ISO uses a different variant.

```bash
#!/bin/bash
set -euo pipefail

# variables (edit)
BASE_ISO=~/ubuntu.iso
WORK=~/zt-iso-work
EXTRACT=~/squashfs-root
OUT=~/ZeroTrace.iso
AOUT=~/a.out
MENU=~/menu.sh
SQUASH_NAME=casper/minimal.standard.live.squashfs

# prepare
mkdir -p ~/mnt_dir ~/iso_dir $WORK $EXTRACT
sudo mount -o loop "$BASE_ISO" ~/mnt_dir
rsync -a ~/mnt_dir/ ~/iso_dir/
sudo umount ~/mnt_dir
cp -rT ~/iso_dir $WORK

# extract squashfs
sudo unsquashfs -d $EXTRACT "$WORK/$SQUASH_NAME"

# install files
sudo mkdir -p "$EXTRACT/usr/local/bin"
sudo cp "$AOUT" "$EXTRACT/usr/local/bin/"
sudo cp "$MENU" "$EXTRACT/usr/local/bin/"
sudo chmod +x "$EXTRACT/usr/local/bin/a.out" "$EXTRACT/usr/local/bin/menu.sh"

# add autorun
sudo tee "$EXTRACT/etc/profile.d/zerotrace.sh" > /dev/null <<'EOF'
#!/bin/sh
if [ -x /usr/local/bin/menu.sh ] && [ -z "$ZEROTRACE_STARTED" ]; then
  export ZEROTRACE_STARTED=1
  /usr/local/bin/menu.sh &
fi
EOF
sudo chmod +x "$EXTRACT/etc/profile.d/zerotrace.sh"

# rebuild squashfs into working tree
sudo mksquashfs "$EXTRACT" "$WORK/$SQUASH_NAME" -comp xz -b 1048576 -Xbcj x86 -noappend

# regenerate md5s (example — adjust filtering)
cd $WORK
sudo rm -f md5sum.txt
find . -type f -print0 | sudo xargs -0 md5sum | sed 's| ./| |' | sudo tee md5sum.txt

# build ISO — choose correct command depending on whether isolinux is present
sudo xorriso -as mkisofs -r -V "ZeroTraceISO" -o "$OUT" -J -l -cache-inodes \
  -eltorito-alt-boot -e boot/grub/efi.img -no-emul-boot -isohybrid-gpt-basdat .

echo "Done: $OUT"
```

---

## Notes for the jury / demo checklist

1. Files added: `/usr/local/bin/a.out`, `/usr/local/bin/menu.sh` — show them inside the live session.
2. Autorun file: `/etc/profile.d/zerotrace.sh` — explain how this triggers the menu.
3. Show the original squashfs name and confirm you replaced it: `casper/minimal.standard.live.squashfs`.
4. Boot the ISO in QEMU first, then use a USB stick for the real machine.
5. Explain Secure Boot implications — unsigned changes may not boot on Secure Boot systems.

---

## Final remarks

This document reproduces, documents and improves upon the sequence you performed in your terminal logs. If you want, I can:

* produce a one-click shell script for the jury machine (with interactive `read -p` confirmations),
* include screenshots of QEMU boot output, or
* tailor the autorun script to launch a graphical menu only when X/Wayland is available.

---

**End of document**
