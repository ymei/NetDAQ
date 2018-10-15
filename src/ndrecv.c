/** \file
 * NetDAQ receiving and dump to memory.
 */
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

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

static time_t startTime, stopTime;
static void signal_kill_handler(int sig)
{
    printf("\nstart time = %zd\n", startTime);
    printf("Stop time  = %zd\n", time(NULL));
    fflush(stdout);

    fprintf(stderr, "Killed, cleaning up...\n");
    alarm(0);
    atexit(atexit_shm_cleanup);
    exit(EXIT_SUCCESS);
}

static size_t wrBytes=0, wrSegs=0;
static unsigned int wrCountInterval=1;
static void signal_alarm_handler(int sig)
{
    size_t b, s;

    signal(SIGALRM, SIG_IGN);
    if(ssv) {
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

    // parse switches
    memcpy(&pm, &paramDefault, sizeof(pm));
    while((optC = getopt(argc, argv, "dl:n:s:")) != -1) {
        switch(optC) {
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

    pageSize = get_system_pagesize();
    shmSize = sizeof(SHM_ELEM_TYPE) * pm.shmSegLen * pm.shmNSeg;
    fprintf(stderr, "System pagesize: %zd bytes.\n", pageSize);
    fprintf(stderr, "Shared memory element size: %zd bytes.\n", sizeof(SHM_ELEM_TYPE));
    fprintf(stderr, "Shared memory SegLen: %zd, NSeg: %zd, total size: %zd bytes.\n",
            pm.shmSegLen, pm.shmNSeg, shmSize);

    if(shmSize <= 0 || (sz = shmSize % pageSize) > 0) {
        fprintf(stderr, "shmSize (%zd) should be multiple of pagesize (%zd).\n",
                shmSize, pageSize);
        shmSize += (pageSize - sz);
        fprintf(stderr, "Enlarge to %zd.\n", shmSize);
    }
    shmfd = shm_create(pm.shmName, &shmp, &shmSize, &ssv, pm.shmRmQ);
    if(shmfd<0 || shmp==NULL) return EXIT_FAILURE;
    close(shmfd); // Can be closed immediately after mmap.
    /* Start. */
    printf("Start time = %zd\n", startTime = time(NULL));
    /* Register for clean up. */
    signal(SIGKILL, signal_kill_handler);
    signal(SIGINT,  signal_kill_handler);
    /* Initialize shm_sync. */
    shm_sync_producer_init(ssv);
    ssv->segLen = pm.shmSegLen;
    ssv->nSeg   = pm.shmNSeg;
    /* For write count */
    signal(SIGALRM, signal_alarm_handler);
    alarm(wrCountInterval);

    SHM_ELEM_TYPE *p;
    uintptr_t i, j=0;
    while(1) {
        p = shm_acquire_next_segment_sync(shmp, ssv, SHM_SEG_WRITE);
        for(i=0; i<ssv->segLen; i++) {
            p[i] = (SHM_ELEM_TYPE)(j);
            j++;
        }
        shm_update_write_count(ssv, ssv->segLen, 1);
    }
    /* Stop. */
    stopTime = time(NULL);
    alarm(0);
    atexit_shm_cleanup();
    return EXIT_SUCCESS;
}
