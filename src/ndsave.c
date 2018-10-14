/** \file
 * NetDAQ saving data to file from shared memory.
 */
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "ipc.h"

/** Parameters settable from commandline */
typedef struct param
{
    char *shmName;   //!< shared memory object name, system-wide.
} param_t;

param_t paramDefault = {
    .shmName = SHM_NAME_DEFAULT,
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
    size_t pageSize, shmSize;
    param_t pm;
    int optC = 0;

    // parse switches
    memcpy(&pm, &paramDefault, sizeof(pm));
    while((optC = getopt(argc, argv, "n:")) != -1) {
        switch(optC) {
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
    shmfd = shm_connect(pm.shmName, &shmp, &shmSize);
    if(shmfd<0 || shmp==NULL) return EXIT_FAILURE;
    printf("shm size is %zd.\n", shmSize);
    printf("shm pointer at %p.\n", shmp);

    volatile uint32_t *p = (volatile uint32_t*)shmp, p0=0;
    for(int i=0;;i++) {
        if(*p != p0) {
            printf("0x%x, i=%d\n", *p, i);
            p0 = *p;
        }
    }
    close(shmfd);
    return EXIT_SUCCESS;
}
