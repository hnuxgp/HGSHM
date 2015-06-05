#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

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
	int	fd;
	size_t	shm_sz;
	size_t	shm_slice_sz;
	void	(*cb) (void *);
	void	*cb_arg;
    void    *shmptr[2];
    int index;
} hgshm_t;

hgshm_t hgshm;

static uint64_t virt_to_phys(void *vmem);

static void sig_handler(int sig)
{
	if(hgshm.cb)
		hgshm.cb(hgshm.cb_arg);
}

int hgshm_notify(int index)
{
	return ioctl(hgshm.fd, HGSHM_POKE, &index);
}

int hgshm_get_index(void)
{
    return hgshm.index;
}

size_t hgshm_get_shm_slice_sz(void)
{
    return hgshm.shm_slice_sz;
}

void hgshm_close(void)
{
	munmap(hgshm.shmptr[0], hgshm.shm_sz);
    if (hgshm.index != 0)
	    munmap(hgshm.shmptr[1], hgshm.shm_slice_sz);
    close(hgshm.fd);
}

void * hgshm_getshm(int index, size_t *sz)
{
    if (index < 0 && index > 1)
        return NULL;
     switch (index) {
     case 0: *sz = hgshm.shm_sz;
        break;
     case 1: *sz = hgshm.shm_slice_sz;
        break;
     }
     return hgshm.shmptr[index];
}

static int hgshm_map(void)
{
    hgshm.shmptr[0] = mmap(0, hgshm.shm_sz, PROT_READ|PROT_WRITE,
        MAP_SHARED|MAP_LOCKED, hgshm.fd, ((4<<10) * 1));

	if (hgshm.shmptr[0] == MAP_FAILED) {
		perror ("");
        printf("MAP_FAILED for shmptr[0]\n");
		close(hgshm.fd);
		return -1;
	}

    if (hgshm.index == 0)
        return 0; /* No BAR3 for zero index */

    hgshm.shmptr[1] = mmap(0, hgshm.shm_slice_sz, PROT_READ|PROT_WRITE,
        MAP_SHARED|MAP_LOCKED, hgshm.fd, ((4<<10) * 3));

	if (hgshm.shmptr[1] == MAP_FAILED) {
		perror ("");
        printf("MAP_FAILED for shmptr[1]\n");
	    munmap(hgshm.shmptr[0], hgshm.shm_sz);
		close(hgshm.fd);
		return -1;
	}
    return 0;
}

int hgshm_init(char *dev, void (*cb)(void *), void *cb_arg)
{
	set_sig_ioctl_t iodata;

	hgshm.fd = open (dev, O_RDWR, 0666);
	if (hgshm.fd <= 0) {
        return -1;
    }

	iodata.pid = getpid();
	iodata.signal = SIGUSR1;

	signal(iodata.signal, sig_handler);
	if (ioctl(hgshm.fd, HGSHM_SET_SIGNAL, &iodata) < 0) {
		perror ("");
		close(hgshm.fd);
        return -1;
	}

	if (ioctl(hgshm.fd, HGSHM_GET_INDEX, &hgshm.index) < 0) {
		perror ("");
		close(hgshm.fd);
        return -1;
	}
    //printf("IDX: %d\n", (int)hgshm.index);

	if (ioctl(hgshm.fd, HGSHM_GET_SHM_SIZE, &hgshm.shm_sz) < 0) {
		perror ("");
		close(hgshm.fd);
        return -1;
	}
    //printf("SZ: %d\n", (int)hgshm.shm_sz);

	if (ioctl(hgshm.fd, HGSHM_GET_SHM_SLICE_SIZE, &hgshm.shm_slice_sz) < 0) {
		perror ("");
		close(hgshm.fd);
        return -1;
	}
    //printf("SLICE_SZ: %d\n", (int)hgshm.shm_slice_sz);
	hgshm.cb = cb;
	hgshm.cb_arg = cb_arg;
    hgshm_map();
	return 0;
}

static uint64_t virt_to_phys(void *vmem)
{
	int pagemap_fd;
	uint64_t pfn, ptbits, physaddr, seek_loc;
	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0) {
		printf("pagemap open error\n");
		exit (1);
	}

	pfn = ((uint64_t) (vmem)) / (4<<10);
	seek_loc = pfn * sizeof (uint64_t);
	if (lseek (pagemap_fd, seek_loc, SEEK_SET) != seek_loc) {
		printf("lseek to 0x%lx error\n", seek_loc);
		exit (1);
	}

	if (read (pagemap_fd, &ptbits, sizeof (ptbits)) != sizeof(ptbits)) {
		printf ("read ptbits\n");
		exit (1);
	}
	physaddr = (ptbits & 0x7fffffffffffffULL) * (4<<10);
	close (pagemap_fd);
	return physaddr;
}
