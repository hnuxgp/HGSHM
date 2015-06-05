#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <assert.h>
#include <math.h>

static int TRUE = 1;
#define MB (1024 * 1024)
#define TCPBUFLEN (4 * MB)
#define MAXBUFLEN (256 * MB)
#define IPV4    4
#define IPV6    6
int counter;

typedef struct _c {
    int fd;
    int port;
    int af;
    union {
        struct sockaddr_in addr4;
        struct sockaddr_in6 addr6;
    } addr;
} cookie_t;

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

static void * prepare(int port, int af)
{
    int rc;
    cookie_t *c = malloc(sizeof(cookie_t));
    c->port = port;
    c->af = af;

    if((c->fd = socket(af, SOCK_STREAM, 0)) == -1) {
        perror("Sock createion failed");
        goto error;
    }

    if (setsockopt(c->fd, SOL_SOCKET, SO_REUSEADDR,
        &TRUE, sizeof(TRUE)) < 0) {
        perror("setsockopt(SOL_SOCKET, SO_REUSEADDR)");
        goto error;
    }

    if (af == AF_INET) {
        memset(&(c->addr.addr4), 0, sizeof(struct sockaddr_in));
        c->addr.addr4.sin_family = AF_INET;
        c->addr.addr4.sin_port = htons(port); /* Server port */
        c->addr.addr4.sin_addr.s_addr = INADDR_ANY;
        rc = bind(c->fd, (struct sockaddr *)&(c->addr.addr4),
            sizeof(struct sockaddr_in));
    } else {
        memset(&(c->addr.addr6), 0, sizeof(struct sockaddr_in6));
        c->addr.addr6.sin6_family = AF_INET6;
        c->addr.addr6.sin6_port = htons(port); /* Server port */
        c->addr.addr6.sin6_addr = in6addr_any;
        rc = bind(c->fd, (struct sockaddr *)&(c->addr.addr6),
            sizeof(struct sockaddr_in6));
    }

    if (rc == -1) {
        perror("Server-bind() error");
        goto error;
    }
	return c;
error:
    if (c->fd > 0)
        close(c->fd); 
    free(c);
    return NULL;
}

static void serverthr(void *arg)
{
    cookie_t *c = (cookie_t *) arg;
    int newfd;
    socklen_t addr_len;
    char *afstr;

    if (c->af == AF_INET) {
        addr_len = sizeof(struct sockaddr_in);
        afstr = "IPv4";
    } else {
        addr_len = sizeof(struct sockaddr_in6);
        afstr = "IPv6";
    }

    printf("Listening %s on TCP port %d....\n", afstr, c->port);
    fflush(stdout);
    listen(c->fd, 1);

accept:
    if (c->af == AF_INET)
        newfd = accept(c->fd, (struct sockaddr *)&(c->addr.addr4), &addr_len);
    else
        newfd = accept(c->fd, (struct sockaddr *)&(c->addr.addr6), &addr_len);

    if (newfd == -1) {
            perror("Server-accept() error");
            pthread_exit(0);
    }

    int numbytes = 0;
    char *buf = malloc(MAXBUFLEN);
    int recvlen = TCPBUFLEN;
    size_t trcvd = 0, total = MAXBUFLEN;
    while (total > 0) {
        if ((numbytes = recv(newfd, buf, recvlen, 0)) < 0) {
            perror("Server-recv() error");
            exit(0);
        }
        total -= numbytes;
        trcvd += numbytes;
        if (total < TCPBUFLEN)
            recvlen = total;
    }
    printf("%d Received: %ld bytes\n", counter++, trcvd);
    int count = dowork(buf, MAXBUFLEN);
    memcpy(buf, &count, sizeof(int));
    if((numbytes = send(newfd, &count, sizeof(int), 0)) == -1)
        perror("Server-send() error");
    close(newfd);
    free(buf);
    goto accept;
}

int main(int argc, char *argv[])
{
    int sport = 10000;
    int  af = AF_INET;
    cookie_t *cookie = prepare(sport, af);
    serverthr(cookie);
    return 0;
}
