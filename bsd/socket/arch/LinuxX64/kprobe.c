#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

/* Static storage for captured register state */
static struct pt_regs captured_regs;
static DEFINE_MUTEX(regs_mutex);
static struct proc_dir_entry *proc_entry;
static bool regs_valid = false;

static int handler_pre(struct kprobe *p, struct pt_regs *regs)
{
    /* Only capture register state on the primary CPU (CPU 0) to avoid 
     * multiple cores overwriting the same data structure */
    if (smp_processor_id() != 0)
        return 0;
        
    /* Capture the real register state */
    mutex_lock(&regs_mutex);
    captured_regs = *regs;
    regs_valid = true;
    mutex_unlock(&regs_mutex);
    
    return 0;
}

/* Procfs read function to export register data to userspace */
static ssize_t proc_read(struct file *file, char __user *buffer, size_t count, loff_t *pos)
{
    int len;
    char output[512];
    
    if (*pos > 0)
        return 0;
    
    mutex_lock(&regs_mutex);
    if (!regs_valid) {
        len = snprintf(output, sizeof(output), "No register data captured yet\n");
    } else {
        len = snprintf(output, sizeof(output),
            "RIP=0x%lx RSP=0x%lx RBP=0x%lx RAX=0x%lx RBX=0x%lx RCX=0x%lx RDX=0x%lx "
            "RSI=0x%lx RDI=0x%lx R8=0x%lx R9=0x%lx R10=0x%lx R11=0x%lx R12=0x%lx "
            "R13=0x%lx R14=0x%lx R15=0x%lx EFLAGS=0x%lx CS=0x%lx SS=0x%lx ORIG_RAX=0x%lx\n",
            captured_regs.ip, captured_regs.sp, captured_regs.bp, captured_regs.ax,
            captured_regs.bx, captured_regs.cx, captured_regs.dx, captured_regs.si,
            captured_regs.di, captured_regs.r8, captured_regs.r9, captured_regs.r10,
            captured_regs.r11, captured_regs.r12, captured_regs.r13, captured_regs.r14,
            captured_regs.r15, captured_regs.flags, captured_regs.cs, captured_regs.ss,
            captured_regs.orig_ax);
    }
    mutex_unlock(&regs_mutex);
    
    if (count < len)
        return -EINVAL;
    
    if (copy_to_user(buffer, output, len))
        return -EFAULT;
    
    *pos = len;
    return len;
}

static const struct proc_ops proc_fops = {
    .proc_read = proc_read,
};

static struct kprobe kp = {
    .symbol_name = "schedule", // More frequently called than tick_handle_periodic
    .pre_handler = handler_pre,
};

static int __init my_kprobe_init(void)
{
    int ret;
    
    /* Create procfs entry */
    proc_entry = proc_create("panel_regs", 0444, NULL, &proc_fops);
    if (!proc_entry) {
        printk(KERN_ERR "Failed to create /proc/panel_regs\n");
        return -ENOMEM;
    }
    
    /* Register kprobe */
    ret = register_kprobe(&kp);
    if (ret < 0) {
        printk(KERN_ERR "register_kprobe failed, returned %d\n", ret);
        proc_remove(proc_entry);
        return ret;
    }
    
    printk(KERN_INFO "Panel kprobe registered for %s, data available at /proc/panel_regs\n", kp.symbol_name);
    return 0;
}

static void __exit my_kprobe_exit(void)
{
    unregister_kprobe(&kp);
    proc_remove(proc_entry);
    printk(KERN_INFO "Panel kprobe unregistered\n");
}

module_init(my_kprobe_init)
module_exit(my_kprobe_exit)
MODULE_LICENSE("GPL");
