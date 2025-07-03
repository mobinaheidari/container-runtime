
# 🐧 Simple Containers Runtime

A lightweight Linux container runtime written in C using namespaces, cgroups v2, overlayfs, and user capabilities. Inspired by `runc`, but built from scratch for learning and experimentation purposes.

---

## 🚀 Features

* User, PID, UTS, NET, IPC namespaces
* Rootless containers (user namespace support)
* Cgroup v2 isolation (CPU, memory, I/O, PID limits)
* OverlayFS-based root filesystem
* CPU pinning and real-time scheduler (SCHED\_RR)
* Metadata management for containers
* Basic CLI interface (`start`, `stop`, `rm`, etc.)
* Host-to-container device mount propagation
* Freezing/thawing containers
* External syscall monitor integration (`monitor.py`)

---

## 🧱 Dependencies

* Linux kernel with:

  * Cgroup v2 support
  * OverlayFS
  * Namespace support
* `gcc`, `make`
* Python 3 (`monitor.py` syscall tracer)

---

## 🔧 Build

```bash
make
```

Or manually:

```bash
gcc -o simple_container simple_container.c -Wall -O2 -lcap
```

---

## 🛠️ Usage

### Start a new container

```bash
sudo ./simple_container start /path/to/rootfs
```

Optional IPC sharing:

```bash
sudo ./simple_container start --share-ipc /path/to/rootfs
```

### List running containers

```bash
./simple_container list
```

### Check status

```bash
./simple_container status <pid>
```

### Stop a container

```bash
./simple_container stop <pid>
```

### Freeze / Thaw

```bash
./simple_container freeze <pid>
./simple_container thaw <pid>
```

### Inspect container metadata

```bash
./simple_container inspect <pid>
```

### Remove container records

```bash
./simple_container rm <pid>
```

### Share device into container(s)

```bash
sudo ./simple_container mountdev /dev/sdx /mnt/device_in_container <pid1> [pid2...]
```

---

## 🧪 Example RootFS

Use `debootstrap`, `busybox`, or a Docker-exported rootfs. Example:

```bash
debootstrap --variant=minbase stable ./myrootfs http://deb.debian.org/debian
```

---

## 📁 Filesystem Layout

* Overlay mount point: `/mnt`
* Upper and workdir: `/tmp/container_<pid>_upper`, `/tmp/container_<pid>_work`
* Cgroup base: `/sys/fs/cgroup/my_runtime/`
* PID tracking: `/run/my_runtime/simple_containers.pids`
* Metadata: `/run/my_runtime/metadata/`

---

## 🛡️ Security

* Drops all capabilities inside container
* Isolated user namespace (maps container UID 0 to real user)
* Uses `setgroups deny` for safe gid\_map
* Uses shared mount propagation carefully

---

## 🧼 Cleanup

If anything breaks, you can clean up residual mounts and directories:

```bash
umount -l /mnt
rm -rf /tmp/container_* /run/my_runtime
```

---

## 🤝 Contributing

This project is for educational and experimental use. Contributions are welcome via issues or PRs. Just keep it simple, minimal, and clean.

---
