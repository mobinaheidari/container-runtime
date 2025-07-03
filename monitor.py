
from bcc import BPF
import time

bpf_source = """
#include <uapi/linux/ptrace.h>

int trace_clone(struct pt_regs *ctx) {
    bpf_trace_printk("CLONE syscall by PID %d\\n", bpf_get_current_pid_tgid() >> 32);
    return 0;
}

int trace_unshare(struct pt_regs *ctx) {
    bpf_trace_printk("UNSHARE syscall by PID %d\\n", bpf_get_current_pid_tgid() >> 32);
    return 0;
}

int trace_setns(struct pt_regs *ctx) {
    bpf_trace_printk("SETNS syscall by PID %d\\n", bpf_get_current_pid_tgid() >> 32);
    return 0;
}

int trace_mount(struct pt_regs *ctx) {
    bpf_trace_printk("MOUNT syscall by PID %d\\n", bpf_get_current_pid_tgid() >> 32);
    return 0;
}

int trace_mkdir(struct pt_regs *ctx) {
    bpf_trace_printk("MKDIR syscall by PID %d\\n", bpf_get_current_pid_tgid() >> 32);
    return 0;
}
"""

b = BPF(text=bpf_source ,cflags=["-w"] )
b.attach_kprobe(event="__x64_sys_clone", fn_name="trace_clone")
b.attach_kprobe(event="__x64_sys_unshare", fn_name="trace_unshare")
b.attach_kprobe(event="__x64_sys_setns", fn_name="trace_setns")
b.attach_kprobe(event="__x64_sys_mount", fn_name="trace_mount")
b.attach_kprobe(event="__x64_sys_mkdir", fn_name="trace_mkdir")



import os


log_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "container.log")
with open(log_path, "a") as logfile:



    print("Monitoring syscalls...")
    while True:
        try:
            (task, pid, cpu, flags, ts, msg) = b.trace_fields()
    
            # Always save everything to the file
            logfile.write(f"[{time.strftime('%H:%M:%S')}] PID {pid}: {msg}\n")
            logfile.flush()
    
            # Only print some syscalls to terminal (example: only print MOUNT and CLONE)
            #msg_str = msg.decode('utf-8')
            #if "MOUNT" in msg_str or "CLONE" in msg_str:

               # print(f"[{time.strftime('%H:%M:%S')}] PID {pid}: {msg}")
    
        except KeyboardInterrupt:
            break

