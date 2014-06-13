#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/init.h>      // included for __init and __exit macros
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SunXi");
MODULE_DESCRIPTION("A Simple module to demostrate how to show kernel memory");

static int showmem_major;
static struct class *showmem_class;
static char *showmem_txt_buf;
static unsigned int showmem_txt_size;
static unsigned int showmem_txt_start;
static ssize_t showmem_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) 
{
  int lcount = count;
  if (showmem_txt_start + lcount >= showmem_txt_size)
    lcount = showmem_txt_size - showmem_txt_start;

  copy_to_user(buf, showmem_txt_buf + showmem_txt_start, lcount);
  showmem_txt_start += lcount;

  if (lcount < count) {
    kfree(showmem_txt_buf);
    showmem_txt_buf = 0;
    showmem_txt_size = showmem_txt_start = 0;
  }

  return lcount;
}

static ssize_t showmem_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) 
{
  char lbuf[32];
  int size, *pstart, *pend;
  char *ptxt, *addr;

  if (count > 30)
    return -EINVAL;
  if (copy_from_user(lbuf, buf, count))
    return -EINVAL;

  lbuf[count] = 0;
  sscanf(lbuf, "%x %d", (int*)&addr, &size);
  if (size > 65536)
    return -EINVAL;

  if (showmem_txt_buf != NULL)
    kfree(showmem_txt_buf);
  showmem_txt_size = (size + 16) * 3;
  showmem_txt_buf = kmalloc(showmem_txt_size, GFP_USER);
  if (showmem_txt_buf == NULL)
    return -EINVAL;

  ptxt = showmem_txt_buf;
  pstart = (int*)addr;
  pend = (int*)(addr + size);

  for(; (unsigned int)pstart < (unsigned int)pend; pstart += 4) {
    ptxt += sprintf(ptxt, "%08x:%08x %08x %08x %08x\n",
		    (unsigned int)pstart, *pstart, *(pstart+1), *(pstart+2), *(pstart+3));
  }

  showmem_txt_start = 0;

  return count;
}

static int showmem_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int showmem_release(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations showmem_fops = {
	.owner = THIS_MODULE,
	.read = showmem_read,
	.write = showmem_write,
	.open = showmem_open,
	.release = showmem_release,
};

static int __init showmem_init(void)
{
	showmem_major = register_chrdev(0, "showmem", &showmem_fops);
	if (showmem_major == 0) {
		printk("unable to register a char device!\n");
	}
	printk("mmap_test got a major id %d\n", showmem_major);

	showmem_class = class_create(THIS_MODULE, "showmem");
	device_create(showmem_class, NULL, MKDEV(showmem_major, 0), NULL, "showmem%d", 0);

	return 0;    // Non-zero return means that the module couldn't be loaded.
}

static void __exit showmem_cleanup(void)
{
    printk(KERN_INFO "Cleaning up module.\n");
    device_destroy(showmem_class, MKDEV(showmem_major, 0));
    class_destroy(showmem_class);
    unregister_chrdev(showmem_major, "showmem");
}

module_init(showmem_init);
module_exit(showmem_cleanup);

