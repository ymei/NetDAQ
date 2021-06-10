/** \file
 * NetDAQ tcp server.  Primarily for generating data to feed ndrecv for testing.
 */
#define _GNU_SOURCE

/* waitpid on linux */
#include <sys/types.h>
#include <sys/wait.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netdb.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>

#if defined(__linux) /* on linux */
#include <pty.h>
#include <utmp.h>
#elif defined(__FreeBSD__)
#include <libutil.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <util.h>
#endif

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h> /* mmap */
#include <unistd.h>
#include <pthread.h>
#include <getopt.h>

#include "common.h"

static int nsfd=0; /**< network socket fd (server) */
static int sock_open(const char *host, const char *port)
{
    int status;
    struct addrinfo addrHint, *addrList, *ap;
    int sockfd, sockopt;

    memset(&addrHint, 0, sizeof(struct addrinfo));
    addrHint.ai_flags     = AI_PASSIVE | AI_ADDRCONFIG | AI_CANONNAME | AI_NUMERICSERV;
    addrHint.ai_family    = AF_INET; /* we deal with IPv4 only, for now */
    addrHint.ai_socktype  = SOCK_STREAM;
    addrHint.ai_protocol  = 0;
    addrHint.ai_addrlen   = 0;
    addrHint.ai_canonname = NULL;
    addrHint.ai_addr      = NULL;
    addrHint.ai_next      = NULL;

    status = getaddrinfo(host, port, &addrHint, &addrList);
    if (status < 0) {
        error_printf("getaddrinfo: %s\n", gai_strerror(status));
        return status;
    }
    for (ap=addrList; ap!=NULL; ap=ap->ai_next) {
        sockfd = socket(ap->ai_family, ap->ai_socktype, ap->ai_protocol);
        if (sockfd < 0) continue;
        sockopt = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&sockopt, sizeof(sockopt)) == -1) {
            close(sockopt);
            warn("setsockopt");
            continue;
        }
        if (bind(sockfd, ap->ai_addr, ap->ai_addrlen) < 0) {
            close(sockfd);
            warn("connect");
            continue;
        } else {
            break; /* success */
        }
    }
    freeaddrinfo(addrList);
    if (ap == NULL) { /* No address succeeded */
        error_printf("Could not bind, tried %s:%s\n", host, port);
        return -1;
    }
    return sockfd;
}

static void sock_close(int sockfd)
{
    close(sockfd);
}

int main(int argc, char **argv)
{
    char *host, *port;
    SHM_ELEM_TYPE *data;
    size_t dlen = 1024*1024;

    host = argv[1];
    port = argv[2];

    if ((nsfd = sock_open(host, port)) < 0) {
        return EXIT_FAILURE;
    }
    if (listen(nsfd, SOMAXCONN) < 0) {
        warn("listen");
        return EXIT_FAILURE;
    }
    int acptfd;
    if ((acptfd = accept(nsfd, NULL, 0)) < 0) {
        warn("accept");
        goto EXIT;
    }
    size_t dsz = dlen * sizeof(SHM_ELEM_TYPE);
    data = malloc(dsz);
    intptr_t j=0;
    for (;;) {
        for (intptr_t i=0; i<dlen; i++) {
            data[i] = j;
            j++;
        }
        send(acptfd, data, dsz, 0);
    }
    free(data);
    close(acptfd);
EXIT:
    sock_close(nsfd);
    return EXIT_SUCCESS;
}
