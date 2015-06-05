#ifndef _HGSHM_H
#define	_HGSHM_H

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>

#define	HGSHM_VENDOR_ID	0xBABE
#define	HGSHM_DEVICE_ID	0x07B9

#define	HGSHM_USER_IO_NOTIFY_REG	0x00	/* size 64bytes */
#define	HGSHM_STATUS_REG		    0x40	/* size 4 */
#define	HGSHM_FEATURES_REG		    0x44	/* size 4 */
#define	HGSHM_SHM_SIZE_REG		    0x48	/* size 4 */
#define	HGSHM_SHM_SLICE_SIZE_REG	0x4C	/* size 4 */
#define	HGSHM_ISR_REG			    0x50	/* size 1 */
#define	HGSHM_IRQ_REG			    0x51	/* size 1 */
#define	HGSHM_IDX_REG			    0x52	/* size 1 */

#define HGSHM_IO_BAR            0
#define HGSHM_MEM_BAR           1
#define HGSHM_SLICE_I_BAR       3

#define	HGSHM_NAME                  "hgshm"
#define	HGSHM_FEATURES_GUEST_MMAP	0x1

#define HGSHM_COUNT             3
#define HGSHM_MAX_DEVS          1

#define HGSHM_SET_SIGNAL	        _IOW('H', 1, set_sig_ioctl_t)
#define HGSHM_POKE                  _IOW('H', 2, int)
#define HGSHM_GET_SHM_SIZE	        _IOR('H', 3, size_t)
#define HGSHM_IRQ_ENABLE	        _IOW('H', 4, int)
#define HGSHM_GET_INDEX             _IOR('H', 5, int)
#define HGSHM_GET_IO_SIZE	        _IOR('H', 6, size_t)
#define HGSHM_GET_SHM_SLICE_SIZE	_IOR('H', 7, size_t)

typedef struct {
	int	signal;
	pid_t	pid;
} set_sig_ioctl_t;

typedef struct {
	set_sig_ioctl_t iodata;
    struct task_struct *task;
} user_data_t;

typedef struct {
    int         type;
    size_t    size;
    uint32_t    mask;
    void __iomem *bar_addr;
    phys_addr_t phys_bar_addr;
} bar_t;

typedef struct hgshm_softc {
    struct pci_dev *pci_dev;
    const struct pci_device_id *pci_id;
    uint16_t init_progress_flag;
    struct cdev cdev;
    user_data_t userdata;
    bar_t       bars[6]; /* 6 pci bars */
    int         index;
    size_t      slice_size;
} hgshm_softc_t;

#define HGSHM_READ1_REG(sc, o)		ioread8((sc)->bars[HGSHM_IO_BAR].bar_addr + (o))
#define HGSHM_READ2_REG(sc, o)		ioread16((sc)->bars[HGSHM_IO_BAR].bar_addr + (o))
#define HGSHM_READ4_REG(sc, o)		ioread32((sc)->bars[HGSHM_IO_BAR].bar_addr + (o))
#define HGSHM_READ8_REG(sc, o)		ioread64((sc)->bars[HGSHM_IO_BAR].bar_addr + (o))

#define HGSHM_WRITE1_REG(sc, o, v)	iowrite8((v), (sc)->bars[HGSHM_IO_BAR].bar_addr + (o))
#define HGSHM_WRITE2_REG(sc, o, v)	iowrite16((v), (sc)->bars[HGSHM_IO_BAR].bar_addr + (o))
#define HGSHM_WRITE4_REG(sc, o, v)	iowrite32((v), (sc)->bars[HGSHM_IO_BAR].bar_addr + (o))
#define HGSHM_WRITE8_REG(sc, o, v)	iowrite64((v), (sc)->bars[HGSHM_IO_BAR].bar_addr + (o))

#define DEV_ENABLED             (0x1 << 0)
#define IO_REGION_ALLOCATED     (0x1 << 1)
#define MEM_REGION_ALLOCATED    (0x1 << 2)
#define IO_REGION_MAPPED        (0x1 << 3)
#define MEM_REGION_MAPPED       (0x1 << 4)
#define IRQ_ENABLED             (0x1 << 5)
#define CDEV_CREATED            (0x1 << 6)
#endif /* _HGSHM_H */
