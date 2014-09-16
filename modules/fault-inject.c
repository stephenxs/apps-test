#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/init.h>      // included for __init and __exit macros
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pid.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SunXi");
MODULE_VERSION("0.1");
MODULE_DESCRIPTION("A Simple module to inject fault");

static int faultinject_major;
static struct class *faultinject_class;
static char *faultinject_txt_buf;
static unsigned int faultinject_txt_size;
static unsigned int faultinject_txt_start;
static ssize_t faultinject_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) 
{
#if 0
  int lcount = count;
  if (faultinject_txt_start + lcount >= faultinject_txt_size)
    lcount = faultinject_txt_size - faultinject_txt_start;

  copy_to_user(buf, faultinject_txt_buf + faultinject_txt_start, lcount);
  faultinject_txt_start += lcount;

  if (lcount < count) {
    kfree(faultinject_txt_buf);
    faultinject_txt_buf = 0;
    faultinject_txt_size = faultinject_txt_start = 0;
  }

  return lcount;
#endif
return 0;
}

void faultinject_mmap_semaphore(const char *arg)
{
  struct mm_struct *mm;
  volatile int i = 0;

  sscanf(arg, "%d", &i);

  //get my mm_struct
  mm = current->mm;
 
  //p the mutex
  down_write(&mm->mmap_sem);

  //simulating a busy task
  trace_printk("starting busy loop %d\n", i);
//  for(; i > 0; i--) cond_resched();
  msleep(i);
  trace_printk("finish busy loop\n");

  //v the mutex and return
  up_write(&mm->mmap_sem);
}

#define FAULTINJECT_HELP		0
#define FAULTINJECT_MMAPSEMAPHORE	1
#define FAULTINJECT_MAX			2
static ssize_t faultinject_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) 
{
  char lbuf[32], *pbuf = lbuf;
  int command;

  if (count > 30)
    return -EINVAL;
  if (copy_from_user(lbuf, buf, count))
    return -EINVAL;

  lbuf[count] = 0;
  pbuf += sscanf(pbuf, "%d", &command);

  if (command >= FAULTINJECT_MAX)
    return -EINVAL;

  switch(command)
  {
  case FAULTINJECT_HELP:
    break;
  case FAULTINJECT_MMAPSEMAPHORE:
    faultinject_mmap_semaphore(pbuf);
    break;
  default:
    break;
  }

  return count;
}

static int faultinject_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int faultinject_release(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations faultinject_fops = {
	.owner = THIS_MODULE,
	.read = faultinject_read,
	.write = faultinject_write,
	.open = faultinject_open,
	.release = faultinject_release,
};

static int __init faultinject_init(void)
{
	faultinject_major = register_chrdev(0, "fault-injection", &faultinject_fops);
	if (faultinject_major == 0) {
		printk("unable to register a char device!\n");
	}
	printk("fault-injection got a major id %d\n", faultinject_major);

	faultinject_class = class_create(THIS_MODULE, "faultinject");
	device_create(faultinject_class, NULL, MKDEV(faultinject_major, 0), NULL, "showmmap%d", 0);

	return 0;    // Non-zero return means that the module couldn't be loaded.
}

static void __exit faultinject_cleanup(void)
{
    printk(KERN_INFO "Cleaning up module.\n");
    device_destroy(faultinject_class, MKDEV(faultinject_major, 0));
    class_destroy(faultinject_class);
    unregister_chrdev(faultinject_major, "faultinject");
}

module_init(faultinject_init);
module_exit(faultinject_cleanup);

