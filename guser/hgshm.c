#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "hgshm.h"
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>
#include <pthread.h>

void *shmptr[2];
size_t	shm_sz;
size_t	shm_slice_sz;
int myindex;
int alldone;
#define MAGIC   0xCAFEF00D
#define GB      (1 << 30)
int  counter;

#undef  POLLING
#define POLLING

#ifdef POLLING
int callbackrunning = 1;
#endif

#define DEBUG
#undef DEBUG

typedef struct _dt { /* data transfer */
    void    *src;
    void    *dst;
    size_t  size;
    int     dindex;
    int     *nservers;
} dtarg_t;

static int thread_create(void *function, void *arg,
    int retries, pthread_t *tid, int detachstate)
{
    int res;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, detachstate);

    while(retries) {
        res = pthread_create(tid, &attr, (void*)function, arg);
        if (res != 0) {
            retries--;
            sleep(1);
        } else break;
    }
    return(res);
}

static int thread_create_detached(void *function, void *arg,
    int retries, pthread_t *tid)
{
    return thread_create(function, arg, retries, tid, PTHREAD_CREATE_DETACHED);
}

static int timediff (struct timeval *t1, struct timeval *t2,
    struct timeval *diff)
{
    int cmp = 0;
    struct timeval result;
    if (diff == NULL)
        diff = &result;

    diff->tv_sec = t2->tv_sec - t1->tv_sec;
    diff->tv_usec = t2->tv_usec - t1->tv_usec;

    if (diff->tv_sec == 0 && diff->tv_usec == 0) {
        /* Do nothing */
    } else if (diff->tv_sec <= 0 && diff->tv_usec <= 0) {
        cmp = -1;
        diff->tv_sec *= -1;
        diff->tv_usec *= -1;
    } else if (diff->tv_sec >= 0 && diff->tv_usec >= 0) {
        cmp = 1;
    } else if (diff->tv_sec < 0 && diff->tv_usec > 0) {
        cmp = -1;
        diff->tv_sec += 1;
        diff->tv_usec = 1000000 - diff->tv_usec;
    } else if (diff->tv_sec > 0 && diff->tv_usec < 0) {
        cmp = 1;
        diff->tv_sec -= 1;
        diff->tv_usec += 1000000;
    } else {
        assert(diff->tv_sec == 0);
        assert(diff->tv_usec == 0);
    }
    return cmp;
}

static int dowork(void *arg, size_t sz)
{
    int i;
    char *ptr = arg;
    int x = 0;
    for (i = 0; i < sz; i++)
        if (ptr[i] == 'S')
            x++;
    return x;
}

static void callback(void *arg)
{
    int nservers = *((int *)(arg));
#ifdef POLLING
    while (callbackrunning) {
#endif
        if (myindex == 0) {
            int j;
            int c = 0;
            for (j = 0; j < nservers; j++) {
                int32_t *count = (int32_t *)(shmptr[0]);
                //printf("count[%d]: %X\n", j, count[j * 2]);
                if (count[j * 2] == MAGIC)
                    c++;
            }
            if (c == nservers) {
                for (j = 0; j < nservers; j++) {
                    int32_t *count = (int32_t *)(shmptr[0]);
                    count[j * 2] = -1;
                }
                alldone = 1;
            }
        } else {
            int32_t *ptr = (int32_t *)shmptr[1];
#ifdef POLLING
            while (ptr[8192 + (myindex * 2)] != MAGIC);
            ptr[8192 + (myindex * 2)] = 0;
#endif
            printf("%d INDEX: %d. Got new work\n", counter++, myindex);
            ptr[(myindex * 2) + 1] = dowork(shmptr[0], shm_slice_sz);
            ptr[(myindex * 2) + 0] = MAGIC;
#ifndef POLLING
//            printf("NOTIFY 0\n");
            if (hgshm_notify(0) < 0)
                printf("Could not notify index 0\n");
#endif
        }
#ifdef POLLING
    }
#endif
}

static void worker0(void *arg)
{
//    printf("Worker %d INDEX: %d. Got new work\n", counter++, myindex);
    int32_t *ptr = (int32_t *)shmptr[0];
    ptr[(myindex * 2) + 1] = dowork(shmptr[0], shm_slice_sz);
    ptr[(myindex * 2) + 0] = MAGIC;
#ifndef POLLING
    callback(arg);
#endif
}

void print_usage(char *pgm, int ec)
{
    printf("Usage: %s <dev> <GB> [num reducers]\n", pgm);
    if (ec)
        exit(ec);
}

void doxfer (void *arg)
{
    dtarg_t *dt = arg;
    bcopy(dt->src, dt->dst, dt->size);
    if (dt->dindex == 0) {
        pthread_t tid;
        if (thread_create_detached(worker0, dt->nservers, 3, &tid) != 0)
            printf ("Could not create thread\n");
    } else {
#ifdef POLLING
        int32_t *ptr = (int32_t *)shmptr[0];
        ptr[8192 + (dt->dindex * 2)] = MAGIC;
#else
//        printf("NOTIFY %d\n", dt->dindex);
        if (hgshm_notify(dt->dindex) < 0)
            printf("Could not notify index %d\n", dt->dindex);
#endif
    }
    free(arg);
    pthread_exit(0);
}


int main (int argc, char *argv[])
{
    if (argc < 3)
        print_usage(argv[0], 1);

    char *dev =  argv[1];
    int gb = atoi(argv[2]);
    int nservers = 0;
    if (argv[3])
        nservers = atoi(argv[3]);

//    printf("Device: %s, GB: %d: NS: %d\n", dev, gb, nservers);

    hgshm_init(dev, callback, &nservers);
    myindex = hgshm_get_index();

    if (strcmp(argv[2], "-h") == 0) {
        printf("%d\n", myindex);
        exit(0);
    }
#ifdef POLLING
    printf("Polling mode\n");
#else
    printf("Interrupt mode\n");
#endif
    shmptr[0] = hgshm_getshm(0, &shm_sz);
    shmptr[1] = hgshm_getshm(1, &shm_slice_sz);

	if (shmptr[0] == NULL) {
		perror ("");
		printf("-> Guest mmap failed\n");
		exit (1);
	}

	if (myindex != 0 && shmptr[1] == NULL) {
		perror ("");
		printf("-> Guest mmap failed for slice\n");
		exit (1);
	}

#ifdef DEBUG
while (1) {
    char c[4];
    int j;
    printf ("-> ");
    //scanf("%c", &c);
    fgets(c, 4, stdin);
    switch (c[0]) {
    case 'p':
        if (myindex == 0) {
            for (j = 0; j <= nservers; j++) {
                int32_t *count = (int32_t *)(shmptr[0] + (shm_slice_sz * j));
                printf ("%d\n", count[0]);
            }
        } else  {
            int32_t *count = (int32_t *)(shmptr[0]);
            printf ("%d\n", count[0]);
        }
        break;
    case 'm':
        if (myindex == 0) {
            int ii, val;
            printf ("Enter index: ");
            scanf("%d", &ii);
            printf ("Enter Val: ");
            scanf("%d", &val);
            int32_t *count = (int32_t *)(shmptr[0] + (shm_slice_sz * ii));
            count[0] = val;
        } else  {
            int idx = 0, val;
            printf ("Mapped Index [y/n]: ");
            fgets(c, 4, stdin);
            printf ("Enter Val: ");
            scanf("%d", &val);
            if (c[0] == 'y')
                idx = 1;
            int32_t *count = (int32_t *)(shmptr[idx]);
            count[0] = val;
        }
        break;
    case 'n': /* Notify */
        printf ("Enter index: ");
        scanf("%d", &j);
        if (hgshm_notify(j) < 0)
            printf("Could not notify index %\n", j);
        break;
    }
}
#else
#ifdef POLLING
    pthread_t tid;
    if (thread_create_detached(callback, &nservers, 3, &tid) != 0)
        printf ("Could not create thread\n");
#endif
    if (myindex == 0) {
        int count = (((uint64_t)gb * GB) / shm_slice_sz) / nservers;
        void *buf = malloc(shm_slice_sz);
        bzero(buf, shm_slice_sz);
        struct timeval start, end, elp;
        gettimeofday(&start, NULL);
        while (count--) {
            int j;
            alldone = 0;
            for (j = 0; j < nservers; j++) {
                pthread_t xfertid;
                dtarg_t *dt = malloc(sizeof(dtarg_t)); /* Freed by thread */
                dt->src = buf;
                dt->dst = shmptr[0] + (shm_slice_sz * j);
                dt->size = shm_slice_sz;
                dt->dindex = j;
                dt->nservers = &nservers;
                if (thread_create_detached(doxfer, dt, 3, &xfertid) != 0)
                    printf ("Could not create xfer thread\n");
            }
            while (! alldone);
        }
        gettimeofday(&end, NULL);
        timediff(&start, &end, &elp);
        uint64_t elapsed = (elp.tv_sec * 1000) + (elp.tv_usec / 1000);
        printf("%d %ld\n", gb, elapsed);
        free(buf);
#ifdef POLLING
        pthread_cancel(tid);
        callbackrunning = 0;
#endif
    } else {
        while (1);
    }
#endif
    usleep(1000);
	hgshm_close();
	return 0;
}
