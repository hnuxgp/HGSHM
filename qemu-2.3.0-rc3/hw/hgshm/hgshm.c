#include <inttypes.h>
#include <sys/mman.h>
#include <sys/eventfd.h>

#include "hw/pci/pci.h"
#include "qemu/error-report.h"
#include "hw/pci/msix.h"
#include "hw/loader.h"
#include "sysemu/kvm.h"
#include "sysemu/char.h"
#include "qemu/range.h"
#include "sysemu/sysemu.h"
#include "hgshm.h"

#include "hw/sysbus.h"

static void set_rd_handler(HGShm *hgshm, int efd, int index);
static int register_fd_notifier(HGShm *hgshm, int efd, int index);
static void unregister_fd_notifier(HGShm *hgshm, int index);
static void hgshm_notifier_read(void *opaque);
static int hgshm_init_pci_bh(HGShm *hgshm);

typedef struct {
    HGShm   *hgshm;
    int     notifier_index;
} handler_arg_t;

/*
 * index    : Index of the VM. Zero for SHM creator.
 * size     : Size of shared mem, ignored for non-zero index
 * clients  : Number of clients. Valid for zero-index VM and ignored
              for other VMs. Size and clients are used to calculate
              slice size of non-zero index VMs
 * chardev  : Specified as server for zero index, non zero as client.
              Used to exchange event fds and slice size information.
              Client closes after exchange so that another client can
              exchange information with the zero-index VM
 * shmid    : Shared memory id.
              If not specified for zero-index, one is created based on UUID
              Should be specified for non-zero index, the one created by
              zero index VM
 * unlink   : Delete the shared memory. Valid only for zero-index
              Ignored for non-zero index VMs
 * guestmmap: Used to prevent mmap completely from qemu cmd line.
              The guest driver can check HGSHM_FEATURE_GUEST_MMAP
              to determine shared memory is allowed or not
 * mapidx   : Valid for non-zero index VM, ignored for zero-index.
              Index of the VM whose memory you want to map into bar2
 */
static Property hgshm_properties[] = {
	DEFINE_PROP_STRING("size", HGShm, sizestr),
	DEFINE_PROP_STRING("shmid", HGShm, shmid),
	DEFINE_PROP_UINT8("unlink", HGShm, unlink, 0),
	DEFINE_PROP_UINT8("guestmmap", HGShm, guestmmap, 0),
	DEFINE_PROP_INT32("index", HGShm, index, -1),
	DEFINE_PROP_CHR("chardev", HGShm, chardev),
	DEFINE_PROP_INT32("mapidx", HGShm, mapidx, -1),
	DEFINE_PROP_UINT8("clients", HGShm, clients, NUM_CLIENTS),
	DEFINE_PROP_END_OF_LIST(),
};

static int ispow2(uint32_t n)
{
    return (!(n & (n - 1)));
}

static int isalligned(uint32_t n, uint32_t allign)
{
    return (n == (n & ~(allign - 1)));
}

static size_t get_slice_size(size_t shmsize, int clients)
{
    size_t max = (256 << 20);
    int ffs = __builtin_ffs(clients); /* ffs = find first set */
    if (ffs <= 0)
        return shmsize;
    shmsize >>= (ffs - 1);
    return (shmsize > max) ? max : shmsize;
}

/*
 * When running on master, client index will
 * the index of the VM that initiated the connection
 * However, when running on the client,the client_index
 * will be the index of self.
 * In case of master, client_index is needed because
 * it has to remember event fds of all clients. Therefore,
 * efds are stored indexed by client ID. When running, on
 * the client, index that we need is self index.
 * Storing happens in set_rd_handler.
 */
static int send_efd(HGShm *hgshm, int client_index)
{
    ivm_pdu_t pdu;
    bzero(&pdu, sizeof(ivm_pdu_t));
    int efd = eventfd(0, 0);
    if (efd <= 0)
        return -1;
    set_rd_handler(hgshm, efd, client_index);
    pdu.index = hgshm->index;
    pdu.efd_type = EFD_MEM_IO;
    if (hgshm->index == 0) {
        pdu.shmsize = hgshm->size;
        pdu.clients = hgshm->clients;
    } else {
        pdu.needefd = 1; /* Asking index 0 to send efds */
    }
    if (qemu_chr_fe_send_msgfd(hgshm->chardev, efd,
        (uint8_t *)&pdu, sizeof(ivm_pdu_t)) < 0)
            return -1;
    return 0;
}

static void
hgshm_pci_reset(DeviceState *d)
{ }

static void
hgshm_exit_pci(PCIDevice *pci_dev)
{
	DO_UPCAST(HGShm, pci_dev, pci_dev);
}

static void
update_intr(HGShm *hgshm, int value)
{
	hgshm->registers.isr = value & HGSHM_ISR_REG_MASK;
	/* Return if interrupts are disabled */
	if (value && ! hgshm->registers.irq)
		return;
	pci_set_irq(&hgshm->pci_dev, value);
}

static void
set_feature(HGShm *hgshm, uint32_t feature)
{
	hgshm->registers.features |= feature;
}

static uint64_t
hgshm_iomem_read(void *opaque, hwaddr addr,
	unsigned size)
{
	uint32_t regval = -1;
	HGShm *hgshm = (HGShm *)opaque;

	switch(addr) {
		case HGSHM_STATUS_REG:
			regval = hgshm->registers.status;
			break;
		case HGSHM_FEATURES_REG:
			regval = hgshm->registers.features;
			break;
		case HGSHM_SHM_SIZE_REG:
			regval = hgshm->registers.shm_size;
			break;
		case HGSHM_SHM_SLICE_SIZE_REG:
			regval = hgshm->registers.shm_slice_size;
			break;
		case HGSHM_USER_IO_NOTIFY_REG:
//			regval = hgshm->registers.user_notify;
			break;
		case HGSHM_ISR_REG:
			regval = hgshm->registers.isr;
			update_intr(hgshm, 0);
			break;
		case HGSHM_IRQ_REG:
			regval = hgshm->registers.irq;
			break;
		case HGSHM_IDX_REG:
			regval = hgshm->registers.idx;
			break;
	}
	return (uint64_t)regval;
}

static void
notify_explicit(HGShm *hgshm, int index)
{
	uint64_t value = 1;
	int efd = event_notifier_get_fd(&hgshm->notifiers[index][EFD_MEM_IO]);
	if (efd > 0)
		if (write(efd, &value, sizeof(value)) < 0)
		    error_report("Write failed in notify_explicit: %s", hgshm->shmid);
}

static void
hgshm_iomem_write(void *opaque, hwaddr addr, uint64_t val,
	 unsigned size)
{
	HGShm *hgshm = (HGShm *)opaque;
    int index = -1;
    if (addr < HGSHM_STATUS_REG) {
        index = addr - HGSHM_USER_IO_NOTIFY_REG;
        addr = HGSHM_USER_IO_NOTIFY_REG;
    }

	switch(addr) {
		case HGSHM_STATUS_REG:
		case HGSHM_FEATURES_REG:
		case HGSHM_SHM_SIZE_REG:
		case HGSHM_SHM_SLICE_SIZE_REG:
		case HGSHM_IDX_REG:
			break;
		case HGSHM_USER_IO_NOTIFY_REG:
		/*
		 * Event FD is configured for this PIO. Therefore, any write
		 * to this reg will cause KVM to directly trigger an event to
		 * the registered external program. However, if that fails, the
		 * write will cause VM exit and qemu will get control here. In
		 * that case, explicity notify the external program.
		 */
			notify_explicit(hgshm, index);
			break;
		case HGSHM_ISR_REG:
			hgshm->registers.isr = (uint8_t)val;
			break;
		case HGSHM_IRQ_REG:
			hgshm->registers.irq = (uint8_t)val;
			if (hgshm->registers.irq && hgshm->registers.isr)
				update_intr(hgshm, 1);
			break;
	}
}

static const MemoryRegionOps hgshm_iomem_ops = {
	.read = hgshm_iomem_read,
	.write = hgshm_iomem_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.impl = {
		.min_access_size = 1,
		.max_access_size = 4,
		},
};

static void
hgshm_char_event(void *arg, int event)
{ }

static int
hgshm_char_can_read(void *arg)
{
    /* Return the buffer size that you expect in read */
	return (sizeof(ivm_pdu_t));
}

/*
 * This function works slightly different in case of
 * zero-index VM and nonzero-index VM
 */

/*
 * XXX
 * If the slice size is greater than 128MB, guest kernel
 * is not able to correctly map it. Need to figure out
 * what the hell is the problem. For now, put the cap
 * for the size
 */
#define MAXSLICESZ  (128 << 20) /* 128 MB */

static void
hgshm_char_read(void *arg, const uint8_t *buf, int size)
{
	HGShm *hgshm = (HGShm *) arg;
	int efd =  qemu_chr_fe_get_msgfd(hgshm->chardev);
    ivm_pdu_t *pdu = (ivm_pdu_t *)buf;

    error_report("efd: %d, index: %d, type: %d, shmsize: %d, clients: %d",
        efd, pdu->index, pdu->efd_type, (int)pdu->shmsize, pdu->clients);

    if (pdu->efd_type == EFD_MEM_IO) {
        if (hgshm->index != 0) {
            hgshm->size = get_slice_size(pdu->shmsize, pdu->clients);
            /* For non-zero index, size = slice_size */
	        hgshm->registers.shm_slice_size = hgshm->size;
            if (hgshm->registers.shm_slice_size > (MAXSLICESZ)) {
                printf("Down sizing slice size to 0x%X from 0x%lX\n",
                    MAXSLICESZ, hgshm->size);
                hgshm->registers.shm_slice_size = MAXSLICESZ;
            }

            hgshm->clients = pdu->clients;
            if (hgshm->index >= hgshm->clients) {
                error_report("FATAL: Index greater than number of clients!\n");
                exit(1);
            }
            hgshm_init_pci_bh(hgshm);
        }
        register_fd_notifier(hgshm, efd, pdu->index);
    } else if (pdu->efd_type == EFD_RD_HANDLER) {
        set_rd_handler(hgshm, efd, pdu->index);
    }

    if (hgshm->index == 0) {
        if (pdu->needefd && send_efd(hgshm, pdu->index))
            error_report("Sending EFD to %d failed!", pdu->index);
    } else {
        qemu_chr_delete(hgshm->chardev);
    }
}

static size_t
parse_sizestr(char *str)
{
	char *alpha;
	uint64_t multiple = 1;

	if ((alpha = strchr(str, 'k'))) {
		multiple = 1024;
	} else if ((alpha = strchr(str, 'm'))) {
		multiple = 1024 * 1024;
	} else if ((alpha = strchr(str, 'g'))) {
		multiple = 1024 * 1024 * 1024;
	}
	if (alpha)
		*alpha = 0;
	return (atoi(str) * multiple);
}

static char *get_uuid_str(uint8_t* uuid)
{
	char * uuid_str = malloc(UUID_STR_SIZE); /* (16 * 2) + 4 dashes + '\0' */
	/* Example: 550e8400-e29b-41d4-a716-44665544abf6 */
	sprintf(uuid_str, UUID_FMT,
		qemu_uuid[0],  qemu_uuid[1],  qemu_uuid[2],  qemu_uuid[3],
		qemu_uuid[4],  qemu_uuid[5],  qemu_uuid[6],  qemu_uuid[7],
		qemu_uuid[8],  qemu_uuid[9],  qemu_uuid[10],  qemu_uuid[11],
		qemu_uuid[12],  qemu_uuid[13],  qemu_uuid[14],  qemu_uuid[15]);
	return uuid_str;
}

static void hgshm_notifier_read(void *opaque)
{
    handler_arg_t *harg = (handler_arg_t *)opaque;
	HGShm *hgshm = harg->hgshm;
    int index = harg->notifier_index;

	event_notifier_test_and_clear(&hgshm->notifiers[index][EFD_RD_HANDLER]);
	update_intr(hgshm, 1);
}

static void set_rd_handler(HGShm *hgshm, int efd, int index)
{
    handler_arg_t *harg = malloc(sizeof(handler_arg_t));

    harg->hgshm = hgshm;
    harg->notifier_index = index;

    if (hgshm->notifiers[index][EFD_RD_HANDLER].rfd > 0) { /* Unregister */
        qemu_set_fd_handler(hgshm->notifiers[index][EFD_RD_HANDLER].rfd,
            NULL, NULL, NULL);
    }
    hgshm->notifiers[index][EFD_RD_HANDLER].rfd = efd;
    qemu_set_fd_handler(efd, hgshm_notifier_read, NULL, harg);
}

static void unregister_fd_notifier(HGShm *hgshm, int index)
{
    uint32_t    reg_offset = HGSHM_USER_IO_NOTIFY_REG + index;
	memory_region_del_eventfd(&hgshm->bar_iomem, reg_offset,
		1, true, 1, &hgshm->notifiers[index][EFD_MEM_IO]);
}

static int register_fd_notifier(HGShm *hgshm, int efd, int index)
{
	/*
	 * memory_region_add_eventfd evenetually calls kvm_set_ioeventfd_pio_word
	 * which hardcodes the size to be 2. Therefore, what ever value you set here
	 * for size will be discarded. So HGSHM_USER_IO_NOTIFY_REG is of size 2, and
	 * guest writes using outw instead of outl. Else, the size of the write
	 * does not match with the size registered and therefore, event will not be
	 * signaled by KVM.
	 */
    uint32_t    reg_offset = HGSHM_USER_IO_NOTIFY_REG + index;
    if (hgshm->notifiers[index][EFD_MEM_IO].rfd > 0) {
        unregister_fd_notifier(hgshm, index);
    }
    hgshm->notifiers[index][EFD_MEM_IO].rfd = efd;
	memory_region_add_eventfd(&hgshm->bar_iomem, reg_offset,
		1, true, 1, &hgshm->notifiers[index][EFD_MEM_IO]);
	return 0;
}

/*
 * This routine does the actual PCI initialization
 */
static int hgshm_init_pci_bh(HGShm *hgshm)
{
	int fd = 0;
	off_t shm_offset;

    assert(hgshm->size > 0);

    if (hgshm->mapidx > (hgshm->clients - 1)) {
        error_report("mapidx %d too large (ignored). Max %d",
            hgshm->mapidx, hgshm->clients - 1);
        /*
         * On error make mapidx same as index so that mapping to bar2
         * is ignored later
         */
        hgshm->mapidx = hgshm->index;
    }

	fd = shm_open(hgshm->shmid, O_RDWR, 0777);
	if (fd > 0 && hgshm->unlink) {
		close(fd);
		shm_unlink(hgshm->shmid);
		fd = -1;
	}

	if (fd < 0 && hgshm->index == 0)
		fd = shm_open(hgshm->shmid, O_CREAT|O_RDWR, 0777);
	else
		fprintf(stderr, "shmid: %s is in use. Use unlink to delete\n",
			hgshm->shmid);

	if (fd < 0) {
		perror("");
		error_report("Could not open shm object: %s", hgshm->shmid);
		exit(1);
	}

	if (hgshm->index == 0 && ftruncate(fd, hgshm->size)) {
		perror("");
		error_report("Could not truncate shm object: %s", hgshm->shmid);
		exit(1);
	}

    shm_offset = hgshm->index * hgshm->size;

    if (hgshm->guestmmap) {
		hgshm->shmem_map = mmap(0, hgshm->size, PROT_READ|PROT_WRITE,
			MAP_SHARED|MAP_LOCKED, fd, shm_offset);

		/* Clear guest section of memory */
		if (hgshm->zeroit) {
			bzero(hgshm->shmem_map, hgshm->size);
		}

        /* This will use madvice to set MADV_HUGEPAGE and MADV_DONTFORK */
        memory_region_init_ram_ptr(&hgshm->bar_shmem, OBJECT(hgshm),
            "shmem", hgshm->size, hgshm->shmem_map);
    } else {
        memory_region_init_ram(&hgshm->bar_shmem, OBJECT(hgshm),
            "memio", hgshm->size, &error_abort);
    }

    pci_register_bar(&hgshm->pci_dev, HGSHM_MEM_BAR,
        PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64,
        &hgshm->bar_shmem);

    if (hgshm->index != 0 && hgshm->mapidx >= 0 &&
        (hgshm->mapidx != hgshm->index) && hgshm->guestmmap) {

        uint32_t slicesz = hgshm->registers.shm_slice_size;

        shm_offset = slicesz * hgshm->mapidx;
        printf("INDEX: %d, mapidx: %d, offset: %d, sz: %d\n",
            hgshm->index, hgshm->mapidx, (int)shm_offset,
            (int)slicesz);

        hgshm->shmem_slice_map = mmap(0, slicesz, PROT_READ|PROT_WRITE,
            MAP_SHARED|MAP_LOCKED, fd, shm_offset);

        memory_region_init_ram_ptr(&hgshm->bar_slice,
            OBJECT(hgshm), "shmem_slice", slicesz,
            hgshm->shmem_slice_map);

        pci_register_bar(&hgshm->pci_dev, HSGHM_SLICE_I_BAR,
            PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64,
            &hgshm->bar_slice);
    }

	/* region for IOMEM */
	memory_region_init_io(&hgshm->bar_iomem, OBJECT(hgshm), &hgshm_iomem_ops, hgshm,
	    "hgshm-iomem", IOMEM_SIZE);
	pci_register_bar(&hgshm->pci_dev, HGSHM_IO_BAR,
        PCI_BASE_ADDRESS_SPACE_IO, &hgshm->bar_iomem);

	close(fd);
	hgshm->pci_dev.config[PCI_INTERRUPT_PIN] = 1; /* interrupt pin A */
    return 0;
}

/*
 * @hgshm_init_pci:
 *
 * This routine does some initial checks and in case of VM index 0,
 * immediately calls the bottom half of the initialization routine.
 * PCI initialization is split in to two halves because, the shm
 * slice size depends on the total shm size allocated and number
 * of clients. This information is provided only to zero index VM
 * Therefore, as a part of efd exchange, zero-index VM passes
 * total shm size and number of clients. Once that is obtained
 * the non-zero index VM finishes the bottom half of the
 * PCI initialzation.
 */
static int hgshm_init_pci(PCIDevice *pci_dev)
{
	HGShm *hgshm = DO_UPCAST(HGShm, pci_dev, pci_dev);
	char *uuid_str = get_uuid_str(qemu_uuid);
    size_t slice_size = 0;

	if (! hgshm) {
		free(uuid_str);
		return -1;
	}

    if ((int)hgshm->index < 0) {
        error_report("Index not specified");
        return -1;
    }

    hgshm->size = 0;

    if (hgshm->index == 0) { /* creator of shared memory */
	    if (hgshm->sizestr) {
		    hgshm->size = parse_sizestr(hgshm->sizestr);
        }
        /* Initial size is less then default size, set to default */
        if (hgshm->size < HGSHM_DEFAULT_SIZE) {
            error_report("Shared memory size is too small. Defaulting to: %d",
                HGSHM_DEFAULT_SIZE);
		    hgshm->size = HGSHM_DEFAULT_SIZE;
        }
        /* Has to be page aligned */
        if (! isalligned(hgshm->size, PAGE_SIZE)) {
            error_report("Shm size should %d page alligned", PAGE_SIZE);
            return -1;
        }
        if (! hgshm->chardev) {
            error_report("No associated character device for index 0");
        }
        if (hgshm->unlink) {
            hgshm->zeroit = 1;
        }
        if (hgshm->clients > MAX_CLIENTS) {
            error_report("Number of clients > MAX (%d)\n", MAX_CLIENTS);
            hgshm->clients = MAX_CLIENTS;
        }
        if (! ispow2(hgshm->clients)) {
            error_report("Number of clients should be a power of 2");
            return -1;
        }
        slice_size = get_slice_size(hgshm->size, hgshm->clients);
        if (! isalligned(slice_size, PAGE_SIZE)) {
            error_report("Slice size should be alligned at %d boundary",
                PAGE_SIZE);
            return -1;
        }
        /* zero-index VM maps all into bar1 */
        hgshm->mapidx = 0;
    } else {
        hgshm->zeroit = 1;
        if (hgshm->unlink) {
            error_report("Nonzero index, unlink flag ignored");
            hgshm->unlink = 0;
        }
        if (hgshm->sizestr) {
            error_report("Nonzero index, size flag ignored: Defaulted to: %d",
                (int)hgshm->size);
        }
        if (!hgshm->shmid) {
            error_report("Nonzero index, should specify shmid");
            return -1;
        }
        if (hgshm->chardev) {
            error_report("Character dev ignored for nonzero index");
        }
    }

	hgshm->registers.shm_size = hgshm->size;
	/* Interrupt Enbale */
	hgshm->registers.irq = 1;
    /* Remember index */
	hgshm->registers.idx = hgshm->index;
    /* Slice size will be populated later for non-zero index VMs */
	hgshm->registers.shm_slice_size = slice_size;

	if (! hgshm->shmid) {
		int shmid_sz = strlen("hgshm-") + UUID_STR_SIZE;
		hgshm->shmid = malloc(shmid_sz);
		snprintf(hgshm->shmid, shmid_sz, "hgshm-%s", uuid_str);
		printf("SHMID: %s\n", hgshm->shmid);
	}

	if (hgshm->guestmmap)
		set_feature(hgshm, HGSHM_FEATURE_GUEST_MMAP);

    if (hgshm->chardev) {
        qemu_chr_add_handlers(hgshm->chardev, hgshm_char_can_read,
            hgshm_char_read, hgshm_char_event, hgshm);
    }

//    SysBusDevice *d = SYS_BUS_DEVICE(&pci_dev->qdev);
//    sysbus_init_irq(d, &hgshm->irq);

    if (hgshm->index == 0) {
        hgshm_init_pci_bh(hgshm);
    } else {
        /*
         * For non-zero index VM, punt PCI initialization.
         * Bottom of the initialization is done once we get
         * response to send_efs from zero-index VM as the
         * response contains total shm size and num clients
         * that is required to calculate the slice size
         */
        if (send_efd(hgshm, hgshm->index)) {
            error_report("Sending EFD to master failed!");
            return -1;
        }
    }
	free(uuid_str);
    printf("Return from hgshm_init_pci: INDEX: %d\n", hgshm->index);
	return 0;
}

static void hgshm_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
	k->init = hgshm_init_pci;
	k->exit = hgshm_exit_pci;
	k->vendor_id = PCI_VENDOR_ID_HGSHM;
	k->device_id = PCI_DEVICE_ID_HGSHM;
	k->revision = 0xAF;
	k->class_id = PCI_CLASS_SYSTEM_OTHER;
	dc->reset = hgshm_pci_reset;
	dc->props = hgshm_properties;
}


static TypeInfo hgshm_info = {
	.name	  = "hgshm",
	.parent	= TYPE_PCI_DEVICE,
	.instance_size = sizeof(HGShm),
	.class_init    = hgshm_class_init,
};

static void hgshm_register_types(void)
{
	type_register_static(&hgshm_info);
}

type_init(hgshm_register_types)
