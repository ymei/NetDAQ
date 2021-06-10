/** \file
 * NetDAQ saving data to file from shared memory.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "common.h"
#include "ipc.h"

/** Parameters settable from commandline */
typedef struct param
{
    char *shmName;   //!< shared memory object name, system-wide.
} param_t;

param_t paramDefault = {
    .shmName = SHM_NAME,
};

void print_usage(const param_t *pm, FILE *s)
{
    fprintf(s, "Usage:\n");
    fprintf(s, "      -n shmName [\"%s\"]: Shared memory object name, system-wide.\n", pm->shmName);
}

int main(int argc, char **argv)
{
    int shmfd;
    void *shmp;
    shm_sync_t *ssv;
    size_t pageSize, shmSize;
    param_t pm;
    int optC = 0;

    // parse switches
    memcpy(&pm, &paramDefault, sizeof(pm));
    while ((optC = getopt(argc, argv, "n:")) != -1) {
        switch (optC) {
        case 'n':
            pm.shmName = optarg;
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
    shmfd = shm_connect(pm.shmName, &shmp, &shmSize, &ssv);
    if (shmfd<0 || shmp==NULL) return EXIT_FAILURE;
    close(shmfd); // Can be closed immediately after mmap.
    fprintf(stderr, "System pagesize: %zd bytes.\n", pageSize);
    fprintf(stderr, "Shared memory element size: %zd bytes.\n", ssv->elemSize);
    fprintf(stderr, "Shared memory SegLen: %zd, nSeg: %zd, total size: %zd bytes.\n",
            ssv->segLen, ssv->nSeg, shmSize);
    fprintf(stderr, "Shared memory sync variables in the last %d page.\n", SHM_SYNC_NPAGE);

    shm_consumer_init(ssv);
    SHM_ELEM_TYPE *p;
    for (int i=0;;i++) {
        if ((p = shm_acquire_next_segment_sync(shmp, ssv, SHM_SEG_READ))) {
            printf("0x%08x %2td %2td %d\n", *p,
                   atomic_load(&ssv->iRd), atomic_load(&ssv->iWr), i);
        }
    }

    return EXIT_SUCCESS;
}
