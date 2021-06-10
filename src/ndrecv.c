/** \file
 * NetDAQ receiving and dump to memory.
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
#include "ipc.h"

/** Parameters settable from commandline */
typedef struct param
{
    char   *shmName;            //!< shared memory object name, system-wide.
    size_t  shmSegLen;          //!< shared memory segment length.
    size_t  shmNSeg;            //!< shared memory number of segments.
    int     shmRmQ;             //!< remove shared memory if already exist.
} param_t;

param_t paramDefault = {
    .shmName   = SHM_NAME,
    .shmSegLen = SHM_SEG_LEN,
    .shmNSeg   = SHM_NSEG,
    .shmRmQ    = 0
};

static param_t pm;
static void print_usage(const param_t *pm, FILE *s)
{
    fprintf(s, "Usage:\n");
    fprintf(s, "      -d shmRmQ [%d]: Remove shared memory if already exist.\n", pm->shmRmQ);
    fprintf(s, "      -l shmSegLen [%zd]: Shared memory segment length.\n", pm->shmSegLen);
    fprintf(s, "      -n shmName [\"%s\"]: Shared memory object name, system-wide.\n", pm->shmName);
    fprintf(s, "      -s shmNSeg [%zd]: Shared memory number of segments.\n", pm->shmNSeg);
    fprintf(s, "      host port : TCP host:port to get data from.\n");
}

static void *shmp;
static size_t shmSize;
static shm_sync_t *ssv;
static void atexit_shm_cleanup(void)
{
    if(shmp) {
        munmap(shmp, shmSize);
    }
    shm_unlink(pm.shmName);
}

static int nsfd=0; /**< network socket fd. */
static int sock_connect_retry(int sockfd, const struct sockaddr *addr, socklen_t alen)
{
    const int MAXSLEEP=2;
    int nsec;
    /* Try to connect with exponential backoff. */
    for (nsec = 1; nsec <= MAXSLEEP; nsec <<= 1) {
        if (connect(sockfd, addr, alen) == 0) {
            /* Connection accepted. */
            return(0);
        }
        /*Delay before trying again. */
        if (nsec <= MAXSLEEP/2)
            sleep(nsec);
    }
    return(-1);
}

static int sock_open(const char *host, const char *port)
{
    int status;
    struct addrinfo addrHint, *addrList, *ap;
    int sockfd, sockopt;

    memset(&addrHint, 0, sizeof(struct addrinfo));
    addrHint.ai_flags     = AI_CANONNAME|AI_NUMERICSERV;
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
        if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&sockopt, sizeof(sockopt)) == -1) {
            /* setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (char*)&sockopt, sizeof(sockopt)) */
            close(sockopt);
            warn("setsockopt");
            continue;
        }
        if (sock_connect_retry(sockfd, ap->ai_addr, ap->ai_addrlen) < 0) {
            close(sockfd);
            warn("connect");
            continue;
        } else {
            break; /* success */
        }
    }
    freeaddrinfo(addrList);
    if (ap == NULL) { /* No address succeeded */
        error_printf("Could not connect, tried %s:%s\n", host, port);
        return -1;
    }
    return sockfd;
}

static void sock_close(int sockfd)
{
    close(sockfd);
}

/**
 * @param[in] qmsg query message to be sent to peer to ask for more data.
 * @param[in] dblksz expected datablock size sent by peer after each query.
 */
static int sock_recv_data(int sockfd, void *p, shm_sync_t *ssv,
                          const char *qmsg, size_t qmlen, size_t dblksz)
{
    if (sockfd<0) return -1;

    int maxfd;
    fd_set rfd;
    int nsel;
    ssize_t nr, nw;

    /* query message */
    nw = send(sockfd, qmsg, qmlen, 0);
    if (nw<0) {
        warn("send");
        return (int)nw;
    }

    struct timeval tv, tvc = {
        .tv_sec  = 0,
        .tv_usec = 500000,
    };

    char *buf, *bufp;
    const size_t bufsz = ssv->segLen * ssv->elemSize;
    ssize_t rem, dblki=0;
    int qmsent = 0;

    while (1) {
        do {buf = (char*)shm_acquire_next_segment_sync(p, ssv, SHM_SEG_WRITE);
        } while (buf == NULL);

        bufp = buf;
        rem  = bufsz;
        for (;;) {
            FD_ZERO(&rfd);
            FD_SET(sockfd, &rfd);
            maxfd = sockfd;
            tv = tvc;
            nsel  = select(maxfd+1, &rfd, NULL, NULL, &tv);
            if (nsel < 0 && errno != EINTR) { /* other errors */
                warn("select");
                return nsel;
            }
            if (nsel == 0) { /* timed out */
                warn("select() == 0");
                return -1;
                break;
            }
            if (nsel > 0 && FD_ISSET(sockfd, &rfd)) {
                nr = read(sockfd, bufp, rem);
                // if (rem < 100000) printf("nr = %zd, rem = %zd\n", nr, rem);
                if (nr > 0) {
                    bufp += nr;
                    rem -= nr;
                    /* send query message to peer to ask for more data */
                    dblki += nr;
                    if ((dblki > dblksz/2) && (!qmsent)) {
                        nw = send(sockfd, qmsg, qmlen, 0);
                        if (nw<0) {
                            warn("send");
                            return (int)nw;
                        }
                        qmsent = 1;
                    }
                    if (dblki > dblksz) {
                        dblki -= dblksz;
                        qmsent = 0;
                    }
                    if (rem == 0) break; /* done filling buf */
                } else {
                    warn("read");
                    return -1;
                    break;
                }
            }
        }
        shm_update_write_count(ssv, bufsz, 1);
    }

    return 0;
}

static struct timespec startTime, stopTime;
static void signal_kill_handler(int sig)
{
    clock_gettime(CLOCK_MONOTONIC, &stopTime);
    printf("\nStart time = %zd.%09zd\n", startTime.tv_sec, startTime.tv_nsec);
    printf(  "Stop time  = %zd.%09zd\n", stopTime.tv_sec, stopTime.tv_nsec);
    fflush(stdout);

    fprintf(stderr, "Killed, cleaning up...\n");
    alarm(0);
    sock_close(nsfd);
    atexit(atexit_shm_cleanup);
    exit(EXIT_SUCCESS);
}

static size_t wrBytes=0, wrSegs=0;
static unsigned int wrCountInterval=1;
static void signal_alarm_handler(int sig)
{
    size_t b, s;

    signal(SIGALRM, SIG_IGN);
    if (ssv) {
        shm_get_write_count(ssv, &b, &s);
        printf("Bytes wr: %15zd, rate: %7.1f MiB/s; ",
               b, (b-wrBytes)/(wrCountInterval * 1024 * 1024.0));
        printf("Segs wr: %8zd, rate: %5zd/s\n", s, s-wrSegs);
        wrBytes = b;
        wrSegs  = s;
    }
    signal(SIGALRM, signal_alarm_handler);
    alarm(wrCountInterval);
}

int main(int argc, char **argv)
{
    int shmfd;
    size_t pageSize, sz=0;
    int optC = 0;
    char *host, *port;

    // parse switches
    memcpy(&pm, &paramDefault, sizeof(pm));
    while ((optC = getopt(argc, argv, "dl:n:s:")) != -1) {
        switch (optC) {
        case 'd':
            pm.shmRmQ = 1;
            break;
        case 'l':
            pm.shmSegLen = strtoull(optarg, NULL, 10);
            break;
        case 'n':
            pm.shmName = optarg;
            break;
        case 's':
            pm.shmNSeg = strtoull(optarg, NULL, 10);
            break;
        default:
            print_usage(&pm, stderr);
            return EXIT_FAILURE;
            break;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc < 2) {
        fprintf(stderr, "host and port needed!\n");
        return EXIT_FAILURE;
    }

    host = argv[0];
    port = argv[1];

    if ((nsfd = sock_open(host, port))<0) {
        fprintf(stderr, "TCP connection to %s:%s failed.\n", host, port);
        return EXIT_FAILURE;
    }

    pageSize = get_system_pagesize();
    shmSize = sizeof(SHM_ELEM_TYPE) * pm.shmSegLen * pm.shmNSeg;
    fprintf(stderr, "System pagesize: %zd bytes.\n", pageSize);
    fprintf(stderr, "Shared memory element size: %zd bytes.\n", sizeof(SHM_ELEM_TYPE));
    fprintf(stderr, "Shared memory SegLen: %zd, nSeg: %zd, total size: %zd bytes.\n",
            pm.shmSegLen, pm.shmNSeg, shmSize);

    if (shmSize <= 0 || (sz = shmSize % pageSize) > 0) {
        fprintf(stderr, "shmSize (%zd) should be multiple of pagesize (%zd).\n",
                shmSize, pageSize);
        shmSize += (pageSize - sz);
        fprintf(stderr, "Enlarge to %zd.\n", shmSize);
    }
    shmfd = shm_create(pm.shmName, &shmp, &shmSize, &ssv, pm.shmRmQ);
    if (shmfd<0 || shmp==NULL) return EXIT_FAILURE;
    close(shmfd); // Can be closed immediately after mmap.
    /* Start. */
    clock_gettime(CLOCK_MONOTONIC, &startTime);
    printf("Start time = %zd.%09zd\n", startTime.tv_sec, startTime.tv_nsec);
    /* Register for clean up. */
    signal(SIGKILL, signal_kill_handler);
    signal(SIGINT,  signal_kill_handler);
    /* Initialize shm */
    shm_producer_init(ssv);
    ssv->segLen = pm.shmSegLen;
    ssv->nSeg   = pm.shmNSeg;
    /* For write count */
    signal(SIGALRM, signal_alarm_handler);
    alarm(wrCountInterval);

    /*
    SHM_ELEM_TYPE *p;
    uintptr_t i, j=0;
    while (1) {
        p = shm_acquire_next_segment_sync(shmp, ssv, SHM_SEG_WRITE);
        for (i=0; i<ssv->segLen; i++) {
            p[i] = (SHM_ELEM_TYPE)(j);
            j++;
        }
        shm_update_write_count(ssv, ssv->segLen * ssv->elemSize, 1);
    }
    */
    sock_recv_data(nsfd, shmp, ssv, "a\n", 2, 64*1024*1024);

    /* Stop. */
    alarm(0);
    sock_close(nsfd);
    atexit_shm_cleanup();
    return EXIT_SUCCESS;
}
