#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/capability.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>

#define STACK_SIZE (1024 * 1024)
#define PID_STORE_FILE "/run/my_runtime/simple_containers.pids"
#define CGROUP_V2_BASE "/sys/fs/cgroup/my_runtime"
#define METADATA_DIR "/run/my_runtime/metadata"


static char child_stack[STACK_SIZE];

static int cpu_counter = 0;

struct child_args {
    const char *rootfs_path;
    char *binary_path;
};

static void drop_capabilities(void) {
    cap_t caps = cap_init();
    if (cap_set_proc(caps) == -1) {
        perror("cap_set_proc");
        cap_free(caps);
        exit(EXIT_FAILURE);
    }
    cap_free(caps);
}

static int mount_proc(void) {
    return mount("proc", "/proc", "proc", 0, "") == -1 ? (perror("mount /proc"), -1) : 0;
}

static int setup_overlayfs(const char *rootfs_path) {
    char upperdir[256], workdir[256], mount_opts[1024];
    snprintf(upperdir, sizeof(upperdir), "/tmp/container_%d_upper", getpid());
    snprintf(workdir, sizeof(workdir), "/tmp/container_%d_work", getpid());

    mkdir(upperdir, 0755);
    mkdir(workdir, 0755);

    snprintf(mount_opts, sizeof(mount_opts),
             "lowerdir=%s,upperdir=%s,workdir=%s",
             rootfs_path, upperdir, workdir);

    if (mount("overlay", "/mnt", "overlay", 0, mount_opts) == -1) {

        perror("mount overlay");

        return -1;

    }


   
   

    if (mount(NULL, "/mnt", NULL, MS_SHARED | MS_REC, NULL) == -1) {

        perror("mount propagation for /mnt");

        return -1;

    }



    return 0;
}

static void save_metadata(pid_t pid, const struct child_args *args) {
    mkdir(METADATA_DIR, 0755);  // Ensure metadata directory exists

    char path[256];
    snprintf(path, sizeof(path), "%s/%d.meta", METADATA_DIR, pid);
    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "pid=%d\n", pid);
    fprintf(f, "rootfs=%s\n", args->rootfs_path);
    fprintf(f, "memory_limit=100MB\n");
    fprintf(f, "cpu_quota=50%%\n");
    fprintf(f, "overlay_upper=/tmp/container_%d_upper\n", pid);
    fprintf(f, "overlay_work=/tmp/container_%d_work\n", pid);
    fprintf(f, "mount_propagation=shared\n");

    fclose(f);
}

static void cleanup_overlayfs(void) {
    umount2("/mnt", MNT_DETACH);  // Unmount overlay mount

    char upperdir[256], workdir[256];
    snprintf(upperdir, sizeof(upperdir), "/tmp/container_%d_upper", getpid());
    snprintf(workdir, sizeof(workdir), "/tmp/container_%d_work", getpid());

    rmdir(upperdir);
    rmdir(workdir);
}

static int child_func(void *arg) {
    struct child_args *args = (struct child_args *)arg;
    printf("[child] PID %d running\n", getpid());

    // 1. Setup overlayfs *before* chroot
    if (setup_overlayfs(args->rootfs_path) == -1) {
        fprintf(stderr, "[child] setup_overlayfs failed\n");
        return -1;
    }

    // 2. chroot into mounted overlay rootfs
    if (chroot("/mnt") == -1 || chdir("/") == -1) {
        perror("[child] chroot or chdir");
        return -1;
    }

    // 3. Mount proc inside new rootfs
    if (mount_proc() == -1) return -1;

    // 4. Set hostname inside UTS namespace
    if (sethostname("simple-container", strlen("simple-container")) == -1) {
        perror("[child] sethostname");
        return -1;
    }

    // 5. Drop capabilities
    drop_capabilities();

    // 6. Execute bash
    char *const exec_args[] = {"/bin/bash", NULL};
    execv("/bin/bash", exec_args);


    perror("[child] execv /bin/bash");
    cleanup_overlayfs();
    return -1;
}


static void save_pid(pid_t pid) {
    FILE *f = fopen(PID_STORE_FILE, "a");
    if (f) {
        fprintf(f, "%d\n", pid);
        fclose(f);
    }
}

static void remove_pid(pid_t pid_to_remove) {
    FILE *in = fopen(PID_STORE_FILE, "r");
    if (!in) return;

    FILE *out = fopen("/tmp/pid_temp.txt", "w");
    if (!out) {
        fclose(in);
        return;
    }

    char line[32];
    while (fgets(line, sizeof(line), in)) {
        pid_t pid = atoi(line);
        if (pid != pid_to_remove)
            fprintf(out, "%d\n", pid);
    }

    fclose(in);
    fclose(out);

    rename("/tmp/pid_temp.txt", PID_STORE_FILE);
}

static void list_pids(void) {
    FILE *f = fopen(PID_STORE_FILE, "r");
    if (!f) {
        printf("No running containers found.\n");
        return;
    }

    printf("Running containers (PIDs):\n");
    char line[32];
    while (fgets(line, sizeof(line), f)) {
        int pid = atoi(line);
        if (pid > 0 && kill(pid, 0) == 0)
            printf("  PID %d\n", pid);
    }
    fclose(f);
}

static int setup_cgroup_v2(pid_t pid) {
    char cgroup_path[256], path[300];
    FILE *f;

    snprintf(cgroup_path, sizeof(cgroup_path), CGROUP_V2_BASE "/simple_container_%d", pid);
    mkdir(CGROUP_V2_BASE, 0755);
    if (mkdir(cgroup_path, 0755) == -1 && errno != EEXIST) {
        perror("mkdir cgroup v2");
        return -1;
    }

    snprintf(path, sizeof(path), "%s/memory.max", cgroup_path);
    f = fopen(path, "w"); if (f) { fprintf(f, "%lld", 100LL * 1024 * 1024); fclose(f); }

    snprintf(path, sizeof(path), "%s/memory.swap.max", cgroup_path);
    f = fopen(path, "w"); if (f) { fprintf(f, "0"); fclose(f); }

    snprintf(path, sizeof(path), "%s/cpu.max", cgroup_path);
    f = fopen(path, "w"); if (f) { fprintf(f, "50000 100000"); fclose(f); }

    snprintf(path, sizeof(path), "%s/io.max", cgroup_path);
    f = fopen(path, "w"); if (f) { fprintf(f, "default rbps=52428800 wbps=52428800\n"); fclose(f); }

    snprintf(path, sizeof(path), "%s/pids.max", cgroup_path);
    f = fopen(path, "w"); if (f) { fprintf(f, "32"); fclose(f); }

    snprintf(path, sizeof(path), "%s/cgroup.procs", cgroup_path);
    f = fopen(path, "w"); if (f) { fprintf(f, "%d", pid); fclose(f); }

    return 0;
}

static void cleanup_cgroup_v2(pid_t pid) {
    char cgroup_path[256];
    snprintf(cgroup_path, sizeof(cgroup_path), CGROUP_V2_BASE "/simple_container_%d", pid);
    rmdir(cgroup_path);
}



static int setup_user_namespace(pid_t pid) {
    char path[256];
    FILE *f;

    snprintf(path, sizeof(path), "/proc/%d/setgroups", pid);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "deny");
        fclose(f);
    }

    snprintf(path, sizeof(path), "/proc/%d/uid_map", pid);
    f = fopen(path, "w");
    if (!f) {
        perror("uid_map");
        return -1;
    }
    fprintf(f, "0 %d 1\n", getuid());
    fclose(f);

    snprintf(path, sizeof(path), "/proc/%d/gid_map", pid);
    f = fopen(path, "w");
    if (!f) {
        perror("gid_map");
        return -1;
    }
    fprintf(f, "0 %d 1\n", getgid());
    fclose(f);

    return 0;
}
static void remove_metadata(pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%d.meta", METADATA_DIR, pid);
    unlink(path);
}

static int do_start(int argc, char *argv[]) {
    int share_ipc = 0;

    // Check for optional --share-ipc flag
    if (argc >= 2 && strcmp(argv[1], "--share-ipc") == 0) {
        share_ipc = 1;
        argc--; argv++; 
    }

   if (argc != 2) {
    fprintf(stderr, "Usage: start [--share-ipc] <rootfs_path>\n");
    return 1;
    }
    
    const char *rootfs = argv[1];
    
    struct child_args args = {
        .rootfs_path = rootfs,
        .binary_path = NULL,
    };

    // Base namespaces
    int flags = CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET | SIGCHLD;

    // Conditionally add IPC namespace
    if (!share_ipc) {
        flags |= CLONE_NEWIPC;
    }

    pid_t pid = clone(child_func, child_stack + STACK_SIZE, flags, &args);
    if (pid == -1) {
        perror("clone");
        return 1;
    }

    long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    
    if (num_cpus <= 0) num_cpus = 1; 
    
    
    int target_cpu = pid % num_cpus;
     

    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(target_cpu, &cpuset);

    if (sched_setaffinity(pid, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity failed");
      
    } else {
        printf("[parent] Container pinned to CPU %d\n", target_cpu);
    }
    struct sched_param param;
    param.sched_priority = 10; // Set your desired RT priority here
    
    if (sched_setscheduler(pid, SCHED_RR, &param) == -1) {
        perror("sched_setscheduler failed");
    } else {
        printf("[parent] Container PID %d set to SCHED_RR (round-robin) with priority %d\n", pid, param.sched_priority);
    }
    if (setup_user_namespace(pid) == -1) {
        fprintf(stderr, "[parent] Failed to setup user namespace\n");
        kill(pid, SIGKILL);
        cleanup_overlayfs();
        return 1;
    }

    printf("[parent] Container started with PID %d\n", pid);
    save_pid(pid);
    save_metadata(pid, &args);
    if (setup_cgroup_v2(pid) == -1)
        fprintf(stderr, "[parent] Failed to configure cgroups\n");

    int status;
    waitpid(pid, &status, 0);
    printf("[parent] Container exited with status %d\n", WEXITSTATUS(status));
    cleanup_cgroup_v2(pid);
    remove_pid(pid);

    return 0;
}


static int do_list(int argc, char *argv[]) {
    list_pids();
    return 0;
}

static int do_status(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: status <pid>\n");
        return 1;
    }

    pid_t pid = atoi(argv[1]);
    if (kill(pid, 0) == 0)
        printf("Container with PID %d is running\n", pid);
    else
        printf("Container with PID %d is not running\n", pid);

    return 0;
}

static int do_stop(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: stop <pid>\n");
        return 1;
    }

    pid_t pid = atoi(argv[1]);
    if (kill(pid, SIGKILL) == -1)
        perror("kill");
    else
        printf("Sent SIGKILL to container %d\n", pid);

    cleanup_cgroup_v2(pid);
    remove_pid(pid);
    remove_metadata(pid);

    return 0;
}

static int do_inspect(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: inspect <pid>\n");
        return 1;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/%s.meta", METADATA_DIR, argv[1]);

    FILE *f = fopen(path, "r");
    if (!f) {
        perror("inspect");
        return 1;
    }

    printf("Metadata for container PID %s:\n", argv[1]);
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        printf("  %s", line);
    }
    fclose(f);
    return 0;
}

static int do_freeze(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: freeze <pid>\n");
        return 1;
    }

    char path[256];
    snprintf(path, sizeof(path), CGROUP_V2_BASE "/simple_container_%s/cgroup.freeze", argv[1]);
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("freeze");
        return 1;
    }
    fprintf(f, "1");
    fclose(f);
    printf("Container %s frozen\n", argv[1]);
    return 0;
}

static int do_thaw(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: thaw <pid>\n");
        return 1;
    }

    char path[256];
    snprintf(path, sizeof(path), CGROUP_V2_BASE "/simple_container_%s/cgroup.freeze", argv[1]);
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("thaw");
        return 1;
    }
    fprintf(f, "0");
    fclose(f);
    printf("Container %s thawed\n", argv[1]);
    return 0;
}

static int do_rm(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: rm <pid>\n");
        return 1;
    }

    pid_t pid = atoi(argv[1]);
    if (pid <= 0) {
        fprintf(stderr, "Invalid PID: %s\n", argv[1]);
        return 1;
    }

    if (kill(pid, 0) == 0) {
        printf("Killing container process %d...\n", pid);
        if (kill(pid, SIGKILL) == -1) {
            perror("kill");
        } else {
            waitpid(pid, NULL, 0);
            printf("Process %d killed.\n", pid);
        }
    } else {
        printf("Process %d is already dead.\n", pid);
    }

    cleanup_cgroup_v2(pid);
    remove_pid(pid);
    remove_metadata(pid);

    printf("Container %d removed from records and cgroup.\n", pid);
    return 0;
}

int mount_shared_device(pid_t pid, const char *src, const char *target_in_container) {
    char mount_ns_path[256];
    snprintf(mount_ns_path, sizeof(mount_ns_path), "/proc/%d/ns/mnt", pid);

    int mount_ns_fd = open(mount_ns_path, O_RDONLY);
    if (mount_ns_fd == -1) {
        perror("open mount namespace");
        return -1;
    }

    int orig_ns_fd = open("/proc/self/ns/mnt", O_RDONLY);
    if (orig_ns_fd == -1) {
        perror("open original mount namespace");
        close(mount_ns_fd);
        return -1;
    }

    if (setns(mount_ns_fd, 0) == -1) {
        perror("setns to container mount namespace");
        close(mount_ns_fd);
        close(orig_ns_fd);
        return -1;
    }

    // Ensure the target directory exists inside container
    char container_path[256];
    snprintf(container_path, sizeof(container_path), "/mnt%s", target_in_container);  

    mkdir(container_path, 0755);

    if (mount(src, container_path, NULL, MS_BIND, NULL) == -1) {
        perror("bind mount shared device");
        // restore original namespace
        setns(orig_ns_fd, 0);
        close(mount_ns_fd);
        close(orig_ns_fd);
        return -1;
    }

   
    setns(orig_ns_fd, 0);
    close(mount_ns_fd);
    close(orig_ns_fd);
    return 0;
}


int main(int argc, char *argv[]) {
    pid_t monitor_pid = fork();
if (monitor_pid == 0) {
    execlp("python3", "python3", "monitor.py", NULL);
    perror("Failed to launch syscall monitor");
    exit(1);
}

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        fprintf(stderr, "Commands: start, list, status, stop, freeze, thaw, rm\n");
        return 1;
    }

    const char *cmd = argv[1];
    argc--; argv++;

    if (!strcmp(cmd, "start")) return do_start(argc, argv);
if (!strcmp(cmd, "list")) return do_list(argc, argv);
if (!strcmp(cmd, "status")) return do_status(argc, argv);
if (!strcmp(cmd, "stop")) return do_stop(argc, argv);
if (!strcmp(cmd, "inspect")) return do_inspect(argc, argv);
if (!strcmp(cmd, "freeze")) return do_freeze(argc, argv);
if (!strcmp(cmd, "thaw")) return do_thaw(argc, argv);
if (!strcmp(cmd, "rm")) return do_rm(argc, argv);
if (!strcmp(cmd, "mountdev")) {
    if (argc < 4) {
        fprintf(stderr, "Usage: mountdev <device_path> <mount_point_in_container> <pid1> [pid2] ...\n");
        return 1;
    }

    const char *device_path = argv[1];
    const char *target_path = argv[2];

    // Mount device to host path once
    const char *shared_mount_path = "/mnt/shared_dev";
    mkdir(shared_mount_path, 0755);
    if (access(device_path, F_OK) == -1) {
    fprintf(stderr, "Device %s does not exist\n", device_path);
    return 1;
}

    if (mount(device_path, shared_mount_path, NULL, MS_RELATIME, NULL) == -1) {
        perror("mount device to host");
        return 1;
    }

    // Make /mnt shared on the host side to allow propagation to container mounts
if (mount(NULL, "/mnt", NULL, MS_SHARED | MS_REC, NULL) == -1) {
    perror("making /mnt shared");
    return 1;
}

    
    if (mount(NULL, shared_mount_path, NULL, MS_SHARED | MS_REC, NULL) == -1) {
        perror("making shared mount shared");
        return 1;
    }
    

    
    for (int i = 3; i < argc; i++) {
        pid_t pid = atoi(argv[i]);
        if (pid <= 0) continue;
        if (mount_shared_device(pid, shared_mount_path, target_path) == 0)
            printf("Mounted device to container %d at %s\n", pid, target_path);
    }

    return 0;
}



    fprintf(stderr, "Unknown command: %s\n", cmd);
    return 1;
}
