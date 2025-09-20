#!/bin/bash

# ZeroTrace: Making old devices live twice!
echo "🚀 ZeroTrace: Detecting connected devices..."
DEVICES=$(lsblk -o NAME,MOUNTPOINT,SIZE,TYPE | grep disk)

if [ -z "$DEVICES" ]; then
    echo "❌ No devices detected!"
    exit 1
fi

echo "🔌 Connected devices:"
echo "$DEVICES"

# Ask user which device to use
read -p "Enter the device name to process (e.g., sdc): " DEVICE

# Prepend /dev/ to the device name if not already present
if [[ "$DEVICE" != /dev/* ]]; then
    DEVICE="/dev/$DEVICE"
fi

# Run a.out with only the device as the argument
if [ -f "./a.out" ]; then
    echo "✨ Running ZeroTrace on $DEVICE..."
    sudo ./a.out "$DEVICE"
    echo "✅ ZeroTrace operation completed for $DEVICE!"
else
    echo "❌ Error: a.out not found!"
    exit 1
fi
