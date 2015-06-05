#ifndef _HGSHM_H
#define	_HGSHM_H

#include <qemu/event_notifier.h>

#define	HGSHM_USER_IO_NOTIFY_REG	0x00	/* size 64bytes */
#define	HGSHM_STATUS_REG		    0x40	/* size 4 */
#define	HGSHM_FEATURES_REG		    0x44	/* size 4 */
#define	HGSHM_SHM_SIZE_REG		    0x48	/* size 4 */
#define	HGSHM_SHM_SLICE_SIZE_REG	0x4C	/* size 4 */
#define	HGSHM_ISR_REG			    0x50	/* size 1 */
#define	HGSHM_IRQ_REG			    0x51	/* size 1 */
#define	HGSHM_IDX_REG			    0x52	/* size 1 */

#define	HGSHM_ISR_REG_MASK		0xFF
#define	HGSHM_IRQ_REG_MASK		0xFF

#define MAX_CLIENTS             64  /* Should be pow2 */
#define NUM_CLIENTS             8
#define	HGSHM_DEFAULT_SIZE		(512 << 20) /* 512 MB */

#define HGSHM_USER_IO_NOTIFY_REG_SIZE
#define	HGSHM_STATUS_IE_MASK		0x1
#define	HGSHM_STATUS_IE_SHIFT		0

#define	UUID_STR_SIZE			37

#define	HGSHM_FEATURE_GUEST_MMAP	0x1
#define LOCK_NAME_LEN			64

#define HGSHM_IO_BAR            0
#define HGSHM_MEM_BAR           1
#define HSGHM_SLICE_I_BAR       3
#define PAGE_SIZE               (4<<10)

/* Efd type */
#define EFD_RD_HANDLER          0
#define EFD_MEM_IO              1

typedef struct {
    int index;      /* Client index. Always > 1 < 64 */
    int efd_type;   /* Efd type */
    int needefd;    /* set when request is from a VM */
    size_t  shmsize; /* Value sent by zero-index VM */
    int     clients; /* Value sent by zero-index VM */
} ivm_pdu_t;

typedef	struct {
	uint32_t	status;
	uint32_t	features;
	uint32_t	shm_size;
	uint32_t	shm_slice_size;
	uint8_t		isr;
	uint8_t		irq;
	uint8_t		idx;
	uint8_t     user_notify[MAX_CLIENTS];
    char        padding[45];
} hgshm_reg_t;

#define	IOMEM_SIZE	(sizeof(hgshm_reg_t))

typedef struct {
	PCIDevice	    pci_dev;
	CharDriverState *chardev;
	MemoryRegion	bar_shmem;
	MemoryRegion	bar_iomem;
	void 		    *shmem_map;
    /* Below 2 fields are used only for non-zero index */
	MemoryRegion	bar_slice;
	void 		    *shmem_slice_map;
	size_t		    size;
	char		    *shmid;
	char		    *sizestr;
	uint8_t		    unlink;
	uint8_t		    guestmmap;
	int             index; /* Self index. 0 for forwarder */
    /* Valid for non-index VM. Index of the the VM whose mem is mapped */
    int             mapidx;
    uint8_t         clients;
	hgshm_reg_t	    registers;
    /*
     * Total MAX_CLIENTS, 2 for each.
     * TO_FORWARDER and FROM_FORWARDER
     */
	EventNotifier	notifiers[MAX_CLIENTS][2];
    int             zeroit;
} HGShm;

#endif /* _HGSHM_H */
