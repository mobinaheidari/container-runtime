

# Define the rootfs path
ROOTFS_DIR="/tmp/my_container_rootfs"  # Changed path!

echo "Creating container rootfs at: $ROOTFS_DIR"

# 0. Ensure the directory exists, move if needed
if [ ! -d "$ROOTFS_DIR" ]; then
  echo "Directory $ROOTFS_DIR does not exist, creating it"
  sudo mkdir -p "$ROOTFS_DIR"
fi

# 1. Create ALL necessary directories and device files (except device files)
sudo mkdir -p "$ROOTFS_DIR/bin"
sudo mkdir -p "$ROOTFS_DIR/usr/bin"
sudo mkdir -p "$ROOTFS_DIR/lib"
sudo mkdir -p "$ROOTFS_DIR/lib64"
sudo mkdir -p "$ROOTFS_DIR/lib/x86_64-linux-gnu/"
sudo mkdir -p "$ROOTFS_DIR/proc"
sudo mkdir -p "$ROOTFS_DIR/sys"
sudo mkdir -p "$ROOTFS_DIR/tmp"
sudo mkdir -p "$ROOTFS_DIR/dev"
sudo mkdir -p "/run/my_runtime"


# Check if mkdirs were successful
if [ $? -ne 0 ]; then
    echo "Error creating directories. Exiting."
    exit 1
fi

echo "Directories created successfully."

# 2. Copy the binaries
echo "Copying essential binaries..."
sudo cp -v /bin/bash "$ROOTFS_DIR/bin/" || { echo "Failed to copy bash. Exiting."; exit 1; }
sudo cp -v /bin/ls "$ROOTFS_DIR/bin/"   || { echo "Failed to copy ls. Exiting."; exit 1; }
sudo cp -v /bin/sh "$ROOTFS_DIR/bin/"   || { echo "Failed to copy sh. Exiting."; exit 1; }
sudo cp -v /bin/cat "$ROOTFS_DIR/bin/"  || { echo "Failed to copy cat. Exiting."; exit 1; }

# *** New: Copy hostname binary ***
sudo cp -v /usr/bin/hostname "$ROOTFS_DIR/usr/bin/" || { echo "Failed to copy hostname. Exiting."; exit 1; }

# Insert custom program copy and chmod here
sudo cp myprogram "$ROOTFS_DIR/bin/myprogram" || { echo "Failed to copy myprogram. Exiting."; exit 1; }
sudo chmod +x "$ROOTFS_DIR/bin/myprogram"

echo "Binaries copied."

# 3. Copy the necessary libraries
echo "Copying essential libraries..."
sudo cp -v /lib/x86_64-linux-gnu/libtinfo.so.6 "$ROOTFS_DIR/lib/x86_64-linux-gnu/" || { echo "Failed to copy libtinfo. Exiting."; exit 1; }
sudo cp -v /lib/x86_64-linux-gnu/libdl.so.2 "$ROOTFS_DIR/lib/x86_64-linux-gnu/"   || { echo "Failed to copy libdl. Exiting."; exit 1; }
sudo cp -v /lib/x86_64-linux-gnu/libc.so.6 "$ROOTFS_DIR/lib/x86_64-linux-gnu/"   || { echo "Failed to copy libc. Exiting."; exit 1; }
sudo cp -v /lib64/ld-linux-x86-64.so.2 "$ROOTFS_DIR/lib64/" || { echo "Failed to copy ld-linux. Exiting."; exit 1; }

echo "Libraries copied."

# 4. Handle /dev - Bind-mount the entire /dev directory
echo "Mounting /dev..."
if ! mountpoint -q "$ROOTFS_DIR/dev"; then
    sudo mount --bind /dev "$ROOTFS_DIR/dev" || { echo "Failed to bind-mount /dev. Exiting."; exit 1; }
    echo "/dev bind-mounted."
else
    echo "/dev already mounted. Skipping."
fi

# 5. Mount proc and sys (essential for many commands)
echo "Mounting /proc and /sys..."
if ! mountpoint -q "$ROOTFS_DIR/proc"; then
    sudo mount -t proc proc "$ROOTFS_DIR/proc" || { echo "Failed to mount /proc. Exiting."; exit 1; }
    echo "/proc mounted."
else
    echo "/proc already mounted. Skipping."
fi

if ! mountpoint -q "$ROOTFS_DIR/sys"; then
    sudo mount -t sysfs sys "$ROOTFS_DIR/sys" || { echo "Failed to mount /sys. Exiting."; exit 1; }
    echo "/sys mounted."
else
    echo "/sys already mounted. Skipping."
fi

# 6. Attempt to enter the chroot environment
echo ""
echo "Attempting to chroot into $ROOTFS_DIR..."
echo "Type 'exit' to leave the chroot environment."
sudo chroot "$ROOTFS_DIR" /bin/bash

# 7. Unmount everything after exiting chroot
echo ""
echo "Exited chroot. Unmounting filesystems..."

# Unmount in reverse order of mounting (or check mountpoint status)
if mountpoint -q "$ROOTFS_DIR/sys"; then
    sudo umount "$ROOTFS_DIR/sys"
    echo "/sys unmounted."
fi

if mountpoint -q "$ROOTFS_DIR/proc"; then
    sudo umount "$ROOTFS_DIR/proc"
    echo "/proc unmounted."
fi

if mountpoint -q "$ROOTFS_DIR/dev"; then
    sudo umount "$ROOTFS_DIR/dev"
    echo "/dev unmounted."
fi

echo "Cleanup complete."
