#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/time.h>
#include <assert.h>

#define MAXBUFLEN 	(256 << 20) /* 264 MB */
#define THREAD_RETRIES 3
#define MB (1024 * 1024)
#define GB  (MB * 1024)
#define IPV4 4
#define IPV6 6

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

static void print_help(char *cmd, int flag)
{
    printf("Usage: %s <sz> <nservers>\n", cmd);
    printf("\tsz       : Size in GB\n");
    printf("\tnservers : Number of servers\n");
    if (flag)
        exit(0);
}

static const char *ips[] = {
    "8.8.8.41", "8.8.8.11", "8.8.8.12", "8.8.8.13",
    "8.8.8.14", "8.8.8.15", "8.8.8.16", "8.8.8.17",
    "8.8.8.18", "8.8.8.19", "8.8.8.20", "8.8.8.21",
    "8.8.8.22", "8.8.8.23", "8.8.8.24", "8.8.8.25",
    "8.8.8.26", "8.8.8.27", "8.8.8.28", "8.8.8.29",
    "8.8.8.30", "8.8.8.31", "8.8.8.32", "8.8.8.33",
    "8.8.8.34", "8.8.8.35", "8.8.8.36", "8.8.8.37",
    "8.8.8.38", "8.8.8.39", "8.8.8.40", "8.8.8.10"
};

typedef struct _c {
    int id;
    const char *ip;
    char *buf;
    int port;
    size_t sz;
} cookie_t;

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

static int thread_create_joinable(void *function, void *arg,
    int retries, pthread_t *tid)
{
    return thread_create(function, arg, retries, tid, PTHREAD_CREATE_JOINABLE);
}

void clientthr(void *arg)
{
    cookie_t *cookie = arg;
    int sockfd;
    struct sockaddr_in addr4;
    struct sockaddr *addr;
    socklen_t addrlen;
    int numbytes;

    bzero(&addr4, sizeof(struct sockaddr_in));
    addr4.sin_family = AF_INET;
    addr4.sin_port = htons(cookie->port);
    inet_pton(AF_INET, cookie->ip, &(addr4.sin_addr));
    addrlen = sizeof(struct sockaddr_in);
    addr = (struct sockaddr *)&addr4;
    char buf[64];

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Client-socket() error!");
        exit(1);
    }
    if(connect(sockfd, addr, addrlen) == -1) {
        perror("Client-connect() error");
        exit(1);
    }
    if((numbytes = send(sockfd, cookie->buf, cookie->sz, 0)) == -1) {
        perror("Client-send() error 1 !");
        exit(1);
    }
    if((numbytes = recv(sockfd, buf, 64, 0)) == -1) {
        perror("Server-recv() error");
    }
    close(sockfd);
    pthread_exit(0);
}

int main(int argc, char *argv[ ])
{
    if (argc < 3)
        print_help(argv[0], 1);

    int insz = atol(argv[1]); /* Total size */
    int nservers = atoi(argv[2]);
    int i;

    size_t sz = ((size_t)insz << 30); /* actual GBs */
    struct timeval elp, start, end;
    gettimeofday(&start, NULL);

    pthread_t *tids = malloc(sizeof(pthread_t) * nservers);
    cookie_t *cookies = malloc(sizeof(cookie_t) * nservers);
    char *buf = malloc(MAXBUFLEN);
    int count = sz / nservers / MAXBUFLEN;
    printf("Count: %d, nservers: %d\n", count, nservers);

    while (count--) {
        for (i = 0; i < nservers; i++) {
            cookies[i].ip = ips[i];
            cookies[i].port = 10000;
            cookies[i].buf = buf;
            cookies[i].sz = MAXBUFLEN;
            cookies[i].id = i;
            if (thread_create_joinable(clientthr, &cookies[i],
                THREAD_RETRIES, &tids[i]) != 0) {
                printf ("Could not create thread: id %d\n", i);
                exit(-1);
            }
        }
        for (i = 0; i < nservers; i++)
            pthread_join(tids[i], NULL);
    }
    gettimeofday(&end, NULL);
    timediff(&start, &end, &elp);
    uint64_t elapsed = (elp.tv_sec * 1000) + (elp.tv_usec / 1000);
    count = sz / nservers / MAXBUFLEN;
    printf("%d %ld\n", insz, elapsed);
    free(cookies);
    free(tids);
    free(buf);
    return 0;
}
