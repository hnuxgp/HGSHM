#include <linux/module.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include "hgshm.h"

MODULE_AUTHOR("Shesha Sreenivasamurthy <shesha@ucsc.edu>");
MODULE_DESCRIPTION(HGSHM_NAME);
MODULE_LICENSE("GPL");
MODULE_VERSION("1");
static int devopen = 0;
static struct class *hgshm_class;
static unsigned int hgshm_major;

static struct pci_device_id hgshm_id_table[] = {
	{ PCI_DEVICE(HGSHM_VENDOR_ID, HGSHM_DEVICE_ID) },
	{ 0 },
};

MODULE_DEVICE_TABLE(pci, hgshm_id_table);

static int hgshm_open(struct inode *inode, struct file *file)
{
    hgshm_softc_t *hsc;
    devopen++;
    try_module_get(THIS_MODULE);
	hsc = container_of(inode->i_cdev, hgshm_softc_t, cdev);
    file->private_data = hsc;
    return 0;
}

static int hgshm_release(struct inode *inode, struct file *file)
{
    if (! devopen) {
        printk(KERN_ALERT "%s not open\n", HGSHM_NAME);
        return (-1);
    }
    devopen--;
    module_put(THIS_MODULE);
    return 0;
}

static int
hgshm_mmap (struct file * file, struct vm_area_struct * vma)
{
    hgshm_softc_t *hsc = (hgshm_softc_t *) file->private_data;
	uint32_t reg_features = HGSHM_READ4_REG(hsc, HGSHM_FEATURES_REG);
    size_t  psize;
    size_t  vsize;
    phys_addr_t phys;
    int bar_num;

    printk(KERN_DEBUG "%s hgshm_mmap\n", HGSHM_NAME);
	if (! reg_features & HGSHM_FEATURES_GUEST_MMAP) {
		/* If hardware does not support mmap-ing, return error */
		printk(KERN_WARNING "HGSHM_FEATURES_GUEST_MMAP not enabled by hardware\n");
		return -EPERM;
	}

    /*
     * mmap offset is specified in pages. Therefore, vma->vm_pgoff will get us
     * 1 when offset specified in mmap is 4K, and 2 if offset specified is 8K.
     * For mapping IO device area, this is used as BAR number
     */
    bar_num = vma->vm_pgoff;
    printk(KERN_DEBUG "BAR NUM: %X\n", (int)bar_num);
    if (bar_num != HGSHM_IO_BAR && bar_num != HGSHM_MEM_BAR &&
        bar_num != HGSHM_SLICE_I_BAR)
        return -EINVAL;

    psize = hsc->bars[bar_num].size;
    vsize = vma->vm_end - vma->vm_start;

    printk(KERN_DEBUG "PSIZE: %X, VSIZE: %X\n", (int)psize, (int)vsize);
    if (vsize > psize)
        return -EINVAL;

    phys =  (hsc->bars[bar_num].phys_bar_addr) >> PAGE_SHIFT;
    printk(KERN_DEBUG "Remapping PHYS: %llX\n", phys);
    if (remap_pfn_range(vma, vma->vm_start, phys, psize,
        vma->vm_page_prot)) {
            return -EAGAIN;
    }
	return 0;
}

static long hgshm_ioctl(struct file *file, /* see include/linux/fs.h */
         unsigned int ioctl_num,    /* number and param for ioctl */
         unsigned long ioctl_param)
{
    int rc = 0;
    hgshm_softc_t *hsc = (hgshm_softc_t *) file->private_data;
    user_data_t *userdata = &hsc->userdata;
	set_sig_ioctl_t *iodata;
	int	*value;

	switch(ioctl_num) {
		case HGSHM_SET_SIGNAL:
			iodata = (set_sig_ioctl_t *) ioctl_param;
			if (current->pid != iodata->pid) {
                printk(KERN_WARNING "%s: %u cannot set signal for pid %u\n",
				    HGSHM_NAME, current->pid, iodata->pid);
				rc = EINVAL;
				break;
			}
            printk(KERN_WARNING "%s: Current PID %u\n", HGSHM_NAME, current->pid);
			memcpy(&userdata->iodata, iodata, sizeof(set_sig_ioctl_t));
			userdata->task = current;
			break;
		case HGSHM_POKE:
			value = (int *) ioctl_param;
			HGSHM_WRITE1_REG(hsc, (HGSHM_USER_IO_NOTIFY_REG + *value), 1);
			break;
		case HGSHM_GET_SHM_SIZE:
			*((size_t *)ioctl_param) = hsc->bars[HGSHM_MEM_BAR].size;
			break;
		case HGSHM_GET_SHM_SLICE_SIZE:
			*((size_t *)ioctl_param) = hsc->slice_size;
			break;
		case HGSHM_IRQ_ENABLE:
			value = (int *) ioctl_param;
			HGSHM_WRITE1_REG(hsc, HGSHM_IRQ_REG, *value);
            break;
		case HGSHM_GET_INDEX:
			*((int *)ioctl_param) = hsc->index;
			break;
		case HGSHM_GET_IO_SIZE:
			*((size_t *)ioctl_param) = hsc->bars[HGSHM_IO_BAR].size;
			break;
    }
    return rc;
}

#ifdef CONFIG_COMPAT
static long compat_hgshm_ioctl(struct file *file, /* see include/linux/fs.h */
         unsigned int ioctl_num,    /* number and param for ioctl */
         unsigned long ioctl_param) {
    return hgshm_ioctl(file, ioctl_num, ioctl_param);
}
#endif

/*
 * This structure will hold the functions to be called
 * when a process does something to the device we
 * created. Since a pointer to this structure is kept in
 * the devices table, it can't be local to
 * init_module. NULL is for unimplemented functions.
 */
static struct file_operations hsc_fops = {
    .owner = THIS_MODULE,
    .open = hgshm_open,
    .release = hgshm_release,  /* a.k.a. close */
    .unlocked_ioctl = hgshm_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl   = compat_hgshm_ioctl,
#endif
    .mmap = hgshm_mmap,
};

static void
destroy_hgshm_dev(hgshm_softc_t *hsc)
{
    int i;
    int minor = MINOR(hsc->cdev.dev);
    printk(KERN_DEBUG "%s hgshm_destroy_dev\n", HGSHM_NAME);
    if (! (hsc->init_progress_flag & CDEV_CREATED)) {
        printk(KERN_DEBUG "%s hgshm_destroy_dev: NOP\n", HGSHM_NAME);
        return;
    }
    for (i = minor; i < minor + HGSHM_MAX_DEVS; i++) {
        printk(KERN_DEBUG "%s hgshm_destroy_dev: hgshm%d\n", HGSHM_NAME, i);
        device_destroy(hgshm_class, MKDEV(hgshm_major, i));
    }
    cdev_del(&hsc->cdev);
}

static int
create_hgshm_dev(hgshm_softc_t *hsc)
{
    int err = 0;
	int minor;

	cdev_init(&hsc->cdev, &hsc_fops);
    hsc->cdev.owner = THIS_MODULE;
    if ((err = cdev_add(&hsc->cdev, MKDEV(hgshm_major, 0), HGSHM_COUNT))) {
        printk(KERN_ERR "%s: Could not add cdev\n", HGSHM_NAME);
        return err;
    }

	for (minor = 0 ; minor < HGSHM_MAX_DEVS; minor++) {
		struct device *dev;
		dev = device_create(hgshm_class, &hsc->pci_dev->dev,
		    MKDEV(hgshm_major, minor), NULL,
		    "hgshm%d", minor);
		if (IS_ERR(dev))
            printk(KERN_ERR "%s: Could not create dev files\n", HGSHM_NAME);
	}
    hsc->init_progress_flag |= CDEV_CREATED;
    return 0;
}

static irqreturn_t
hgshm_intr(int irq, void *arg)
{
	hgshm_softc_t	*hsc = arg;
	user_data_t	*userdata = &hsc->userdata;
    irqreturn_t ret = IRQ_NONE;
    uint8_t reg_isr;

	reg_isr = HGSHM_READ1_REG(hsc, HGSHM_ISR_REG); /* Reading clears ISR */

	if ((reg_isr == 0xFF) || (reg_isr == 0))
		return ret;

	if (userdata->task)
        kill_pid(task_pid(userdata->task), userdata->iodata.signal, 1);

    printk(KERN_DEBUG "%s hgshm_intr. irq: %d\n", HGSHM_NAME, irq);
    ret = IRQ_HANDLED;
    return ret;
}

static void
release_pci_resources(hgshm_softc_t *hsc)
{
    struct pci_dev *pci_dev = hsc->pci_dev;
    uint16_t    *init_progress_flag = &hsc->init_progress_flag;

	pci_set_drvdata(pci_dev, NULL);
    if (*init_progress_flag & IO_REGION_MAPPED)
	    pci_iounmap(pci_dev, hsc->bars[HGSHM_IO_BAR].bar_addr);
    if (*init_progress_flag & MEM_REGION_MAPPED)
	    pci_iounmap(pci_dev, hsc->bars[HGSHM_MEM_BAR].bar_addr);
    if (*init_progress_flag & MEM_REGION_ALLOCATED)
        pci_release_region(pci_dev, HGSHM_MEM_BAR);
    if (*init_progress_flag & IO_REGION_ALLOCATED)
        pci_release_region(pci_dev, HGSHM_IO_BAR);
    if (*init_progress_flag & IRQ_ENABLED)
        free_irq(pci_dev->irq, hsc);
    if (*init_progress_flag & DEV_ENABLED)
	    pci_disable_device(pci_dev);
}

static int populate_bar_info(hgshm_softc_t *hsc)
{
    struct pci_dev *pci_dev = hsc->pci_dev;
    uint32_t bar, mask;
    size_t  size;
    int i, type;
    void __iomem  *bar_addr;
    phys_addr_t phys_bar_addr;
    uint16_t    *init_progress_flag = &hsc->init_progress_flag;

    uint32_t address[] = {
        PCI_BASE_ADDRESS_0,
        PCI_BASE_ADDRESS_1,
        PCI_BASE_ADDRESS_2,
        PCI_BASE_ADDRESS_3,
        PCI_BASE_ADDRESS_4,
        PCI_BASE_ADDRESS_5,
       0};

    for (i = 0; address[i]; i++) {
        pci_read_config_dword(pci_dev, address[i], &bar);
        pci_write_config_dword(pci_dev, address[i], ~0);
        pci_read_config_dword(pci_dev, address[i], &mask);
        pci_write_config_dword(pci_dev, address[i], bar);
        if (!mask) {
            printk(KERN_DEBUG "bar %X not implemented\n", address[i]);
            continue;
        }
        if (mask & PCI_BASE_ADDRESS_SPACE_IO) {
            mask &= PCI_BASE_ADDRESS_IO_MASK;
            size = (~mask + 1) & 0xffff;
            if ((bar_addr = pci_iomap(pci_dev, HGSHM_IO_BAR, 0)) == NULL) {
                printk(KERN_DEBUG "%s IO region map failed\n", HGSHM_NAME);
                return -1;
            }
            phys_bar_addr = pci_resource_start(pci_dev, HGSHM_IO_BAR);
            type = PCI_BASE_ADDRESS_SPACE_IO;
            *init_progress_flag |= IO_REGION_MAPPED;
        } else {
            mask &= PCI_BASE_ADDRESS_MEM_MASK;
            size = ~mask + 1;
            if ((bar_addr = pci_iomap(pci_dev, i, 0)) == NULL) {
                printk(KERN_DEBUG "%s MEM region map failed\n", HGSHM_NAME);
                return -1;
            }
            phys_bar_addr = pci_resource_start(pci_dev, i);
            *init_progress_flag |= MEM_REGION_MAPPED;
            type = PCI_BASE_ADDRESS_SPACE_MEMORY;
        }
        printk(KERN_DEBUG "Bar %d: type:%d size: 0x%lX bar_addr: %p phys: %llX",
            i, type, size, bar_addr, phys_bar_addr);
        hsc->bars[i].type = type;
        hsc->bars[i].size = size;
        hsc->bars[i].mask = mask;
        hsc->bars[i].bar_addr = bar_addr;
        hsc->bars[i].phys_bar_addr = phys_bar_addr;
    }
    return 0;
}

static int
alloc_pci_resources(hgshm_softc_t *hsc)
{
    int err;
    struct pci_dev *pci_dev = hsc->pci_dev;
    uint16_t    *init_progress_flag = &hsc->init_progress_flag;

    *init_progress_flag = 0;

	/* enable the device */
	if ((err = pci_enable_device(pci_dev))) {
        printk(KERN_DEBUG "%s Device enable failed\n", HGSHM_NAME);
        return err;
    }

    *init_progress_flag |= DEV_ENABLED;

	if ((err = pci_request_region(pci_dev, HGSHM_IO_BAR, "hgshm-io"))) {
        printk(KERN_DEBUG "%s IO region request failed\n", HGSHM_NAME);
        return err;
    }
    *init_progress_flag |= IO_REGION_ALLOCATED;

	if ((err = pci_request_region(pci_dev, HGSHM_MEM_BAR, "hgshm-mem"))) {
        printk(KERN_DEBUG "%s Mem region request failed\n", HGSHM_NAME);
        return err;
    }
    *init_progress_flag |= MEM_REGION_ALLOCATED;

    if ((err = populate_bar_info(hsc))) {
        printk(KERN_DEBUG "%s Populating bar info failed\n", HGSHM_NAME);
        return err;
    }

#if 1
    pci_set_master(pci_dev);
    if (dma_supported(&pci_dev->dev, DMA_BIT_MASK(32))) {
        printk(KERN_DEBUG "%s DMA can be enabled\n", HGSHM_NAME);
    } else {
        printk(KERN_DEBUG "%s DMA cannot be enabled\n", HGSHM_NAME);
        pci_clear_master(pci_dev);
    }
#endif

	if ((err = request_irq(pci_dev->irq, hgshm_intr,
        IRQF_SHARED, HGSHM_NAME, hsc))) {
        printk(KERN_DEBUG "%s IRQ request failed\n", HGSHM_NAME);
        return err;
    }
    *init_progress_flag |= IRQ_ENABLED;

    /* Remember the index */
    hsc->index = HGSHM_READ1_REG(hsc, HGSHM_IDX_REG);
    /* Remember shm slice size */
    hsc->slice_size = HGSHM_READ4_REG(hsc, HGSHM_SHM_SLICE_SIZE_REG);
    printk(KERN_DEBUG "IDX: %d, SLICE_SZ: %X\n",
        hsc->index, (int)hsc->slice_size);
	return 0;
}

/* the PCI probing function */
static int __devinit hgshm_probe(struct pci_dev *pci_dev,
				      const struct pci_device_id *id)
{

    int err;
    hgshm_softc_t   *hsc = NULL;

    printk(KERN_DEBUG "%s hgshm_probe\n", HGSHM_NAME);

	if (pci_dev->vendor != HGSHM_VENDOR_ID)
		return -ENODEV;

	if (pci_dev->device != HGSHM_DEVICE_ID)
		return -ENODEV;

    printk(KERN_DEBUG "%s probed successfully: vendor: 0x%X, device: 0x%X\n",
        HGSHM_NAME, HGSHM_VENDOR_ID, HGSHM_DEVICE_ID);

	/* allocate our structure and fill it out */
	hsc = kzalloc(sizeof(hgshm_softc_t), GFP_KERNEL);
	if (hsc == NULL)
		return -ENOMEM;

    hsc->pci_dev = pci_dev;
    hsc->pci_id = id;
	pci_set_drvdata(pci_dev, hsc);
    if ((err = alloc_pci_resources(hsc)) != 0) {
        release_pci_resources(hsc);
        return err;
    }

    if ((err = create_hgshm_dev(hsc)) != 0) {
        release_pci_resources(hsc);
        return err;
    }
    printk(KERN_DEBUG "ACTION FLAG: %X\n", hsc->init_progress_flag);
    return 0;
}

static void __devexit hgshm_remove(struct pci_dev *pci_dev)
{
	hgshm_softc_t *hsc = pci_get_drvdata(pci_dev);
    printk(KERN_DEBUG "%s hgshm_remove\n", HGSHM_NAME);
    destroy_hgshm_dev(hsc);
    release_pci_resources(hsc);
	kfree(hsc);
}

#ifdef CONFIG_PM
static int hgshm_suspend(struct pci_dev *pci_dev, pm_message_t state)
{
	return 0;
}

static int hgshm_resume(struct pci_dev *pci_dev)
{
	return 0;
}
#endif

static struct pci_driver hgshm_driver = {
	.name		= HGSHM_NAME,
	.id_table	= hgshm_id_table,
	.probe		= hgshm_probe,
	.remove		= __devexit_p(hgshm_remove),
#ifdef CONFIG_PM
	.suspend	= hgshm_suspend,
	.resume		= hgshm_resume,
#endif
};

static int __init hgshm_init(void)
{
	int err;
	dev_t dev;

    printk(KERN_DEBUG "%s hgshm_init\n", HGSHM_NAME);
	hgshm_class = class_create(THIS_MODULE, "pci");
	if (IS_ERR(hgshm_class)) {
		err = PTR_ERR(hgshm_class);
		return err;
	}

	err = alloc_chrdev_region(&dev, 0, HGSHM_MAX_DEVS, HGSHM_NAME);
	if (err)
		goto class_destroy;

	hgshm_major = MAJOR(dev);

	err =	pci_register_driver(&hgshm_driver);
	if (err)
		goto chr_remove;
	return 0;

chr_remove:
	unregister_chrdev_region(dev, HGSHM_MAX_DEVS);
class_destroy:
	class_destroy(hgshm_class);
	return err;
}

module_init(hgshm_init);

static void __exit hgshm_exit(void)
{
    printk(KERN_DEBUG "%s hgshm_exit\n", HGSHM_NAME);
	pci_unregister_driver(&hgshm_driver);
	unregister_chrdev_region(MKDEV(hgshm_major, 0), HGSHM_MAX_DEVS);
	class_destroy(hgshm_class);
}

module_exit(hgshm_exit);
