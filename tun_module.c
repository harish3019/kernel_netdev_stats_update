
/*********************************************************************************************
 *   File:             tun_module.c
 *
 *  Description:       This file provides suuport to update kernel netdev stats.
 * 
 * **********************************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <net/ip.h>
#include <linux/dcache.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/net.h>
#include <linux/signal.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <asm/uaccess.h>


/* function prototypes */
void test_func(void);
static int device_open(struct inode *inode, struct file *file);
static int device_close(struct inode *inode, struct file *file);
static int device_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
static inline struct kobject * tunMod_create_sysfs_group(const char * name);
static int device_ioctl(struct inode *inode, struct file *filp, unsigned int command, unsigned long arg);
static unsigned int device_poll (struct file *filp, poll_table *wait);

/* global variables */
static struct class *plp_class; /* pretend /sys/class */
static        dev_t  char_dev;  /* dynamically assigned at registration. */
static struct cdev  *char_cdev; /* dynamically allocated at runtime. */
unsigned int  proc_list_cnt;
struct dev_list mylist;
struct kobject *kobj = NULL;
int use_count=0;
int uiDataReady=0;


static const char fmt_hex[] = "%#x\n";
static const char fmt_long_hex[] = "%#lx\n";
static const char fmt_dec[] = "%d\n";
static const char fmt_udec[] = "%u\n";
static const char fmt_ulong[] = "%lu\n";
static const char fmt_u64[] = "%llu\n";


#define DEBUG_KERNEL_MODULE printk
#define MAX_INT_NAME 30

/* Data strcuture to hold each dev record ,TODO to be moved to header file  */
struct dev_list{
	struct list_head list;
	char name[MAX_INT_NAME-1];
	struct kobject * kobj;		
};


#define sysfs_dir  "pr-s1-eth1"
#define sysfs_data "data"
#define sysfs_reads "buffer_reads"
#define sysfs_writes "buffer_writes"



/* device file operations */
static struct file_operations mod_fops = {
  .owner   = THIS_MODULE,
  .open    = device_open,
  .read    = device_read,
  .release = device_close,
  .unlocked_ioctl   = device_ioctl,
  .poll    = device_poll,
};


/* 
 *   We must define some structures to hold information about the sysfs file:
 *   
 *   What we need to do is something along the lines of:
 *     static struct kobj_attribute kobj_attr_example = {
 *             .attr = {
 *                  .name = "example",
 *                   .mode = S_IRUGO,
 *                },
 *                 .show = sysfs_show,
 *                  .store = NULL,
 *               };
 *             
 *  The following is a shortcut that does exactly that. Beware, the filename
 *  that will be created (example) must be without quotes! This is because
 *   the name example is also used to form the struct name kobj_attr_example.
 */


/* generate a read-only statistics attribute */
#define NETSTAT_ENTRY(name)                                             \
	static ssize_t name##_show(struct device *d,                            \
			struct device_attribute *attr, char *buf)    \
{                                                                       \
	return netstat_show(d, attr, buf,                               \
			offsetof(struct rtnl_link_stats64, name));  \
}                                                                       \
static DEVICE_ATTR_RO(name)


/* generate a show function for simple field */
#define NETDEVICE_SHOW(field, format_string)                            \
	static ssize_t format_##field(const struct net_device *net, char *buf)  \
{                                                                       \
	        return sprintf(buf, format_string, net->stats.field);                 \
}                                                                       \
static ssize_t field##_show(struct kobject *kobj,                         \
		                            struct kobj_attribute *attr, char *buf)   \
{                                                                       \
	if(!kobj && !kobj->name)\
	{ \
	printk(KERN_INFO "Kobj (parent object )  cannot be NULL  [%s:%d]\n",__func__,__LINE__);\
	return -EINVAL;\
	}\
	\
	struct net_device *dev;\
	if ((dev = dev_get_by_name(&init_net, kobj->name)) == NULL)\
	return -EINVAL;\
\
	printk("Func %s Field read from device  is  %ld  \n",__func__,dev->stats.field);\
	dev_put(dev);\
\
	return sprintf(buf, fmt_ulong, dev->stats.field);\
}      \
										\
static ssize_t field##_store(struct kobject *kobj,                         \
		                            struct kobj_attribute *attr, const char *buf, size_t len)   \
{                                                                       \
	int ret = -EINVAL;\
	if(!kobj && !kobj->name && !buf)\
	{ \
	printk(KERN_INFO "Input arguments cannot be NULL  [%s:%d]\n",__func__,__LINE__);\
	return -EINVAL;\
	}\
\
	struct net_device *dev;\
	unsigned long new;\
						\
	ret = kstrtoul(buf, 0, &new);\
	if (ret)\
	return -EINVAL;\
						\
	if(!buf)\
	return -EINVAL;\
						\
	if ((dev = dev_get_by_name(&init_net, kobj->name)) == NULL)\
	return -EINVAL;\
\
	printk("Func %s field read  is  %ld to be updated is  %ld \n",__func__,dev->stats.field,new);\
	dev_put(dev);\
\
	if (!rtnl_trylock())\
		return restart_syscall();\
\
	dev->stats.field=new;\
	\
	rtnl_unlock();\
	return len;             \
}                                                                       \

#define ATTR_RW(_name) \
	struct kobj_attribute kobj_attr_##_name = __ATTR_RW(_name)


#define NETDEVICE_SHOW_RO(field, format_string)                         \
	NETDEVICE_SHOW(field, format_string);                                   \
static DEVICE_ATTR_RO(field)

#define NETDEVICE_SHOW_RW(field, format_string)                         \
	NETDEVICE_SHOW(field, format_string);                                   \
static ATTR_RW(field)


/*
 *  Then we must define a list with device attributes 
 *  that we support in this module. The list with device attributes must be NULL terminated!
 * 
 *  Please note: while we gave the name 'rx_packets' in the _ATTR call,
 *  it has actually made a 'kobj_attr_rx_packets' for us!
 */

NETDEVICE_SHOW_RW(rx_packets,fmt_ulong);
NETDEVICE_SHOW_RW(tx_packets,fmt_ulong);
NETDEVICE_SHOW_RW(rx_bytes,fmt_ulong);
NETDEVICE_SHOW_RW(tx_bytes,fmt_ulong);
NETDEVICE_SHOW_RW(rx_errors,fmt_ulong);
NETDEVICE_SHOW_RW(tx_errors,fmt_ulong);
NETDEVICE_SHOW_RW(rx_dropped,fmt_ulong);
NETDEVICE_SHOW_RW(tx_dropped,fmt_ulong);
NETDEVICE_SHOW_RW(multicast,fmt_ulong);
NETDEVICE_SHOW_RW(collisions,fmt_ulong);
NETDEVICE_SHOW_RW(rx_length_errors,fmt_ulong);
NETDEVICE_SHOW_RW(rx_over_errors,fmt_ulong);
NETDEVICE_SHOW_RW(rx_crc_errors,fmt_ulong);
NETDEVICE_SHOW_RW(rx_frame_errors,fmt_ulong);
NETDEVICE_SHOW_RW(rx_fifo_errors,fmt_ulong);
NETDEVICE_SHOW_RW(rx_missed_errors,fmt_ulong);
NETDEVICE_SHOW_RW(tx_aborted_errors,fmt_ulong);
NETDEVICE_SHOW_RW(tx_carrier_errors,fmt_ulong);
NETDEVICE_SHOW_RW(tx_fifo_errors,fmt_ulong);
NETDEVICE_SHOW_RW(tx_heartbeat_errors,fmt_ulong);
NETDEVICE_SHOW_RW(tx_window_errors,fmt_ulong);
NETDEVICE_SHOW_RW(rx_compressed,fmt_ulong);
NETDEVICE_SHOW_RW(tx_compressed,fmt_ulong);

static struct attribute *netstat_attrs[] = {
	&kobj_attr_rx_packets.attr,
	&kobj_attr_tx_packets.attr,
	&kobj_attr_rx_bytes.attr,
	&kobj_attr_tx_bytes.attr,
	&kobj_attr_rx_errors.attr,
	&kobj_attr_tx_errors.attr,
	&kobj_attr_rx_dropped.attr,
	&kobj_attr_tx_dropped.attr,
	&kobj_attr_multicast.attr,
	&kobj_attr_collisions.attr,
	&kobj_attr_rx_length_errors.attr,
	&kobj_attr_rx_over_errors.attr,
	&kobj_attr_rx_crc_errors.attr,
	&kobj_attr_rx_frame_errors.attr,
	&kobj_attr_rx_fifo_errors.attr,
	&kobj_attr_rx_missed_errors.attr,
	&kobj_attr_tx_aborted_errors.attr,
	&kobj_attr_tx_carrier_errors.attr,
	&kobj_attr_tx_fifo_errors.attr,
	&kobj_attr_tx_heartbeat_errors.attr,
	&kobj_attr_tx_window_errors.attr,
	&kobj_attr_rx_compressed.attr,
	&kobj_attr_tx_compressed.attr,
	NULL   /* need to NULL terminate the list of attributes */
};


static struct attribute_group netstat_group = {
	.name  = "statistics",
	.attrs  = netstat_attrs,
};


/*
 **  tunMod_init: initialize the phony device
 **  Description: This function allocates a few resources (a cdev,
 **  a device, a sysfs class...) in order to register a new device
 **  and populate an entry in sysfs that udev can use to setup a
 **  new /dev/char entry for reading from the fake device.
 **/


ssize_t update_stats_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	/* show on the update stats file is not required ,as purpose is only to create a device on which  
	 * stats updates needs to be enabled*/
	if(!kobj && !kobj->name)
	printk(KERN_INFO "kobj name is NULL [%s:%d]\n",__func__,__LINE__);

	return sprintf(buf, "%s", "test");
	/*Here any value of variable can be written and exposed to user space through this interface
	 * also return value is the size of data written to the buffer*/
}


static ssize_t update_stats_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	static char sysfs_buffer[30];

	printk("len of the buffer is %d line  %d\n",count,__LINE__);
	if(!buf)
	{
		printk("Input Buffer is  NULL %\n");
			return 0;
	}
	/* copy the interface name to local buffer ,observed an issue that last character is junk 
	 * (i.e ^J appended to input string) ,as work around removing the last chacracter */ //TODO
	memcpy(sysfs_buffer, buf, count-1);
	sysfs_buffer[count-1] = '\0';

	if(buf)
		tunMod_create_sysfs_group(sysfs_buffer);

	return count;
}


/*  declare attribute fields for exposing sys file to enable stats on a input device*/
struct kobj_attribute update_stats_attr =
__ATTR_RW(update_stats);


struct attribute *update_stats_attrs[] = {
	&update_stats_attr.attr,
	NULL,
};

struct attribute_group update_attr_group = {
	.attrs = update_stats_attrs,
};



/* Creates a kernel object and associated group entries with it ,takes interface input as name */
static inline struct kobject * tunMod_create_sysfs_group(const char * name)
{
	struct kobject *kobj = NULL;
    	int result = 0;
       struct dev_list *tmp;

	kobj = kobject_create_and_add(name, kernel_kobj);
	if (kobj == NULL)
	{
		printk (KERN_INFO "%s module failed to load: kobject_create_and_add failed\n", sysfs_data);
		return -ENOMEM;
	}

	printk("Created the kobject ,pointer is  %p\n",kobj);

	result=  sysfs_create_group(kobj,&netstat_group);
	if (result != 0)
	{
		/* creating files failed, thus we must remove the created directory! */
		printk (KERN_INFO "%s module failed to load: sysfs_create_group new failed with result %d\n", sysfs_data, result);
		kobject_put(kobj);
		return -ENOMEM;
	}
	printk("Group created ,pointer is  %p interface name is %s\n",kobj,name);

	/* Add the node to the list */
	/* Allocate memory for the node */
	tmp= (struct dev_list *)vmalloc(sizeof(struct dev_list));

	/* Fill name and kernel object pointer  */
    memcpy(&tmp->name,name, MAX_INT_NAME - 1);
	tmp->kobj=kobj;

	list_add(&(tmp->list), &(mylist.list));

	printk("Added to the list ,pointer is  %p\n",kobj);
	return kobj;
}

static int __init tunMod_init(void)
{
    int result = 0;

    /* dynamically allocate a major number to the device */
    if (alloc_chrdev_region(&char_dev, 0, 1, "tunMod"))
       goto error;

    if (0 == (char_cdev = cdev_alloc()))
       goto error;

    printk("Created cdev alloc\n");
    /* setup the name */
    kobject_set_name(&char_cdev->kobj,"tunMod_cdev");

    /* wire up file ops */
    char_cdev->ops = &mod_fops;

    /* add the device  gluing device and operations together*/
    if (cdev_add(char_cdev, char_dev, 1))
    {
       kobject_put(&char_cdev->kobj);
       unregister_chrdev_region(char_dev, 1);
       goto error;
    }
    printk("cdev added \n");
    
    /* create the class */
    plp_class = class_create(THIS_MODULE, "tunModCls");
    if (IS_ERR(plp_class))
    {
       DEBUG_KERNEL_MODULE(KERN_ERR "Error creating Power module class.\n");
       cdev_del(char_cdev);
       unregister_chrdev_region(char_dev, 1);
       goto error;
    }
    
    printk("class created\n");
    /* create dev entry to be exported to userspace via sysfs */
    device_create(plp_class, NULL, char_dev, NULL, "tunMod");

  	/* Initialize the list */  
    INIT_LIST_HEAD(&mylist.list);


    printk("device created\n");

    /* Create a kernel object to represent sys file to enable stats update on a given interface */
    kobj = kobject_create_and_add("update_stats", kernel_kobj);
    if (kobj == NULL)
    {
	    printk (KERN_INFO "%s module failed to load: kobject_create_and_add failed\n", sysfs_data);
	    return -ENOMEM;
    }

    /* create sysfs group */ 	
    result=  sysfs_create_group(kobj,&update_attr_group);
    if (result != 0)
    {
	    /* creating files failed, thus we must remove the created directory! */
	    printk (KERN_INFO "%s module failed to load: sysfs_create_group new failed with result %d\n", sysfs_data, result);
	    kobject_put(kobj);
	    return -ENOMEM;
    }


    printk(KERN_INFO "/sys/kernel/%s/%s created\n", sysfs_dir, sysfs_data);
    return result;
        return 0;
error:
    DEBUG_KERNEL_MODULE(KERN_ERR "tunMod: could not register device.\n");
    return 1;
}




/*
 **  tunMod_exit: uninitialize the phony device
 **  Description: This function frees up any resource that got allocated
 **  at init time and prepares for the driver to be unloaded.
 **/
static void __exit tunMod_exit(void)
{
    struct dev_list *tmp;
    struct list_head *pos, *q;


     /* Free up the sysfs classes, the previously allocated cdev structure and unregistering the
   *  major device node from the system */
    device_destroy(plp_class, char_dev);

    printk("device destroyed \n");
    /* destroy the device */
   class_destroy(plp_class);

    printk("class destroyed \n");
    /* delete the cdev device */
   cdev_del(char_cdev);

    printk("device  deleted  \n");
    /* de register the character  device */
   unregister_chrdev_region(char_dev,1);
    printk("unregistered chr device  \n");

    /* decrement refcount for object. */
    kobject_put(kobj);


    printk("traversing the list using list_for_each()\n");

    /* Delete all the nodes in the list ,first decrement reference count ,
	delete element in the list and finally ,free up the memory */	
    list_for_each_safe(pos, q, &mylist.list){
            tmp= list_entry(pos, struct dev_list, list);
            printk("freeing item name = %s pointer= %p\n", tmp->name, tmp->kobj);
            kobject_put(tmp->kobj);
            list_del(pos);
            vfree(tmp);
    }


}


/* For future implementation ,if any required */
static int device_open(struct inode *inode, struct file *filp)
{
   
    if(use_count)
    {
       return EBUSY;
    }
    use_count++;
    
    return 0;
}
static int device_read (struct file * file, char __user * buf, size_t count, loff_t * ppos)
{
    DEBUG_KERNEL_MODULE("Read  Req from user process Process \n");

  /*  Dac Data is ready set it is FALSE*/   
   uiDataReady = 0;     

   return 0;
}
static unsigned int device_poll (struct file *filp, poll_table *wait)
{
    unsigned int mask=0;
    
    DEBUG_KERNEL_MODULE(KERN_INFO " receieved poll request \n");
    DEBUG_KERNEL_MODULE(KERN_INFO "Sending Status %d \n" ,mask);
    
    if(uiDataReady) 
       mask |= (POLLIN | POLLRDNORM);

    return mask;
}
static int device_close(struct inode *inode, struct file *filp)
{
   
    use_count--;
    DEBUG_KERNEL_MODULE("Closed the Kern Mod Device\n");
    return 0;
}


static int device_ioctl(struct inode *inode, struct file *filp, unsigned int command, unsigned long arg)
{
   void __user *argp = (void __user *)arg;
   switch(command)
   {
    DEBUG_KERNEL_MODULE("Closed the Kern Mod Device\n");

   }
  return 0;
}

/* declare init/exit functions here */
module_init(tunMod_init);
module_exit(tunMod_exit);

MODULE_AUTHOR("Kulcloud");
MODULE_LICENSE("Dual BSD/GPL");

