#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/init.h>      // included for __init and __exit macros
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pid.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SunXi(mr.sunxi@gmail.com)");
MODULE_DESCRIPTION("A Simple module to demostrate how to show kernel memory");

static int showmmap_major;
static struct class *showmmap_class;
static char *showmmap_txt_buf;
static unsigned int showmmap_txt_size;
static unsigned int showmmap_txt_start;
static ssize_t showmmap_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) 
{
  int lcount = count;
  if (showmmap_txt_start + lcount >= showmmap_txt_size)
    lcount = showmmap_txt_size - showmmap_txt_start;

  copy_to_user(buf, showmmap_txt_buf + showmmap_txt_start, lcount);
  showmmap_txt_start += lcount;

  if (lcount < count) {
    kfree(showmmap_txt_buf);
    showmmap_txt_buf = 0;
    showmmap_txt_size = showmmap_txt_start = 0;
  }

  return lcount;
}

#define OUTPUT_BYTES_PER_PAGE    40
static ssize_t showmmap_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) 
{
  char lbuf[32];
  int pagecount, addr, pid;
  char *ptxt, *porg;
  struct mm_struct *mm;
  struct task_struct *tsk;
  pgd_t *pgd;
  pud_t *pud;
  pmd_t *pmd;
  pte_t *pte;

  if (count > 30)
    return -EINVAL;
  if (copy_from_user(lbuf, buf, count))
    return -EINVAL;

  lbuf[count] = 0;
  sscanf(lbuf, "%d %x %d", &pid, &addr, &pagecount);
  if (pagecount > 1024)
    return -EINVAL;

  //align addr to a 4k bounder
  addr = addr & ~PAGE_SIZE;

  //prepare output memory buffer
  if (showmmap_txt_buf != NULL)
    kfree(showmmap_txt_buf);
  showmmap_txt_size = pagecount * OUTPUT_BYTES_PER_PAGE;
  showmmap_txt_buf = kmalloc(showmmap_txt_size, GFP_USER);
  if (showmmap_txt_buf == NULL)
    return -EINVAL;
  ptxt = showmmap_txt_buf;
  porg = ptxt;

  //first, get mm_struct by pid
  rcu_read_lock();
  tsk = pid_task(find_get_pid(pid), PIDTYPE_PID);
  if (!tsk) {
    ptxt += sprintf(ptxt, "can't find process %d\n", pid);
    goto fail;
  }

  mm = tsk->mm;

  while(pagecount > 0) {
    pgd = pgd_offset(mm, addr);
    pud = pud_offset(pgd, addr);
    pmd = pmd_offset(pud, addr);
    pte = pte_offset(pmd, addr);

    if (pte && !(pte_val(*pte) & _PAGE_PRESENT))
      ptxt += sprintf(ptxt, "[%08x]:not mapped\n", addr);
    else
      ptxt += sprintf(ptxt, "[%08x]:phy %08x %d\n", addr, pte_val(*pte), pte_pfn(*pte));

    addr += PAGE_SIZE;
    pagecount--;
  }

 fail:
  rcu_read_unlock();

  showmmap_txt_size = ptxt - porg;
  showmmap_txt_start = 0;

  return count;
}

static int showmmap_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int showmmap_release(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations showmmap_fops = {
	.owner = THIS_MODULE,
	.read = showmmap_read,
	.write = showmmap_write,
	.open = showmmap_open,
	.release = showmmap_release,
};

static int __init showmmap_init(void)
{
	showmmap_major = register_chrdev(0, "showmmap", &showmmap_fops);
	if (showmmap_major == 0) {
		printk("unable to register a char device!\n");
	}
	printk("mmap_test got a major id %d\n", showmmap_major);

	showmmap_class = class_create(THIS_MODULE, "showmmap");
	device_create(showmmap_class, NULL, MKDEV(showmmap_major, 0), NULL, "showmmap%d", 0);

	return 0;    // Non-zero return means that the module couldn't be loaded.
}

static void __exit showmmap_cleanup(void)
{
    printk(KERN_INFO "Cleaning up module.\n");
    device_destroy(showmmap_class, MKDEV(showmmap_major, 0));
    class_destroy(showmmap_class);
    unregister_chrdev(showmmap_major, "showmmap");
}

module_init(showmmap_init);
module_exit(showmmap_cleanup);

