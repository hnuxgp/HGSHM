#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>
#include <assert.h>

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

int main (int argc, char *argv[])
{
    size_t sz = (32ULL << 30);
    int i;
    int nservers = atoi(argv[1]);
    size_t szpervm = sz / nservers;
    size_t slicesz = (1ULL << 30) / nservers;
    if (slicesz > (256 << 20))
        slicesz = 256 << 20;
    int     count = szpervm / slicesz;

    printf ("Severs: %d slicesz: %ld MB, Count: %d\n",
        nservers, (slicesz >> 20), count);
    char *ptr1 = malloc (slicesz);
    struct timeval start, end, elp;
    gettimeofday(&start, NULL);
    for (i = 0; i < count; i++)
        (void)dowork(ptr1, slicesz);
    gettimeofday(&end, NULL);
    timediff(&start, &end, &elp);
    uint64_t elapsed = (elp.tv_sec * 1000) + (elp.tv_usec / 1000);
    printf("%ld\n", elapsed);
    free(ptr1);
    return 0;
}
