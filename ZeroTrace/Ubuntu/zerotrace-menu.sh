#!/bin/bash
# zerotrace-menu.sh
# Interactive wrapper for your a.out (zeroTraceVerified). Must be run as root.
# Usage: sudo ./zerotrace-menu.sh
# It lists block devices (TYPE=disk), lets user select one or multiple, asks for typed confirmation,
# optionally enables --test or --verify, runs a.out and writes logs to /tmp/zerotrace-<dev>.log

set -euo pipefail
IFS=$'\n\t'

AOUT_PATH="$(dirname "$0")/a.out"   # default: a.out next to this script
# If a.out is somewhere else, edit this path or set AOUT_PATH env var before running.

if [[ ! -x "$AOUT_PATH" ]]; then
  echo "ERROR: a.out not found or not executable at: $AOUT_PATH"
  echo "Place your compiled binary named 'a.out' next to this script or update AOUT_PATH."
  exit 2
fi

if [[ $EUID -ne 0 ]]; then
  echo "This tool must be run as root. Use sudo."
  exit 3
fi

# Fetch disks: only TYPE=disk (no partitions)
mapfile -t DISKS < <(lsblk -dn -o NAME,SIZE,MODEL,TYPE | awk '$4=="disk" { printf "%s|%s|%s\n", $1, $2, substr($0, index($0,$3)) }')

if [[ ${#DISKS[@]} -eq 0 ]]; then
  echo "No block devices of type 'disk' found. Exiting."
  exit 4
fi

echo "=== Connected block devices ==="
i=0
for line in "${DISKS[@]}"; do
  name=$(echo "$line" | cut -d'|' -f1)
  size=$(echo "$line" | cut -d'|' -f2)
  model=$(echo "$line" | cut -d'|' -f3)
  echo "[$i] /dev/$name — $size — $model"
  ((i++))
done
echo "---------------------------------"
echo "Select disk(s) to wipe by index (space-separated). Example: 1 2"
read -r -a sel_indices

# Validate selection
VALID_SEL=()
for idx in "${sel_indices[@]}"; do
  if ! [[ "$idx" =~ ^[0-9]+$ ]]; then
    echo "Ignoring invalid selection: $idx"
    continue
  fi
  if (( idx < 0 || idx >= ${#DISKS[@]} )); then
    echo "Ignoring out-of-range selection: $idx"
    continue
  fi
  VALID_SEL+=("$idx")
done

if [[ ${#VALID_SEL[@]} -eq 0 ]]; then
  echo "No valid disks selected. Exiting."
  exit 5
fi

echo
echo "You selected:"
for idx in "${VALID_SEL[@]}"; do
  entry=${DISKS[$idx]}
  name=$(echo "$entry" | cut -d'|' -f1)
  size=$(echo "$entry" | cut -d'|' -f2)
  model=$(echo "$entry" | cut -d'|' -f3)
  echo " -> /dev/$name — $size — $model"
done
echo

# Choose mode
echo "Choose mode: (1) TEST (single chunk)  (2) FULL wipe  (3) FULL + VERIFY"
read -rp "Enter 1, 2 or 3: " mode_choice
case "$mode_choice" in
  1) MODE_ARG="--test"; MODE_LABEL="TEST (single chunk)";;
  2) MODE_ARG=""; MODE_LABEL="FULL WIPE";;
  3) MODE_ARG="--verify"; MODE_LABEL="FULL WIPE + VERIFY";;
  *) echo "Invalid choice. Aborting."; exit 6;;
esac

echo
echo "IMPORTANT SAFETY: The following operations are destructive and irreversible."
echo "For each selected device you will be asked to CONFIRM by typing the device path."
echo

# For each selected disk, require explicit typed confirmation and run a.out
for idx in "${VALID_SEL[@]}"; do
  entry=${DISKS[$idx]}
  name=$(echo "$entry" | cut -d'|' -f1)
  size=$(echo "$entry" | cut -d'|' -f2)
  model=$(echo "$entry" | cut -d'|' -f3)
  devpath="/dev/$name"
  logfile="/tmp/zerotrace-$(date +%Y%m%d-%H%M%S)-${name}.log"

  echo "------------------------------"
  echo "Target: $devpath"
  echo "Model: $model"
  echo "Size : $size"
  echo "Mode : $MODE_LABEL"
  echo
  echo "Type the full device path to CONFIRM (example: $devpath). Type ABORT to skip."
  read -rp "Confirm: " typed
  if [[ "$typed" == "ABORT" ]]; then
    echo "Skipping $devpath (operator chose ABORT)."
    continue
  fi
  if [[ "$typed" != "$devpath" ]]; then
    echo "Confirmation mismatch for $devpath — skipping to avoid accidents."
    continue
  fi

  echo "Running zeroTrace on $devpath — logs -> $logfile"
  echo "COMMAND: $AOUT_PATH $devpath $MODE_ARG" | tee "$logfile"
  # Run the binary and tee stdout/stderr to logfile so user sees live output
  # Use stdbuf -oL to line-buffer output if available (keeps realtime logs)
  if command -v stdbuf >/dev/null 2>&1; then
    stdbuf -oL "$AOUT_PATH" "$devpath" $MODE_ARG 2>&1 | tee -a "$logfile"
  else
    "$AOUT_PATH" "$devpath" $MODE_ARG 2>&1 | tee -a "$logfile"
  fi

  rc=${PIPESTATUS[0]:-0}
  if [[ $rc -ne 0 ]]; then
    echo "zeroTrace returned non-zero exit code: $rc" | tee -a "$logfile"
  else
    echo "zeroTrace completed for $devpath" | tee -a "$logfile"
  fi

  echo "Flushing caches and syncing..."
  sync && echo "sync done" | tee -a "$logfile"

  echo "Log saved to: $logfile"
  echo "------------------------------"
done

echo
echo "All selected operations finished. Reboot or poweroff as required."
exit 0
