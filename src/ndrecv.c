/** \file
 * NetDAQ receiving and dump to memory.
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
    size_t shmSize;  //!< shared memory size in bytes.
    int shmRmQ;      //!< remove shared memory if already exist.
} param_t;

param_t paramDefault = {
    .shmName = SHM_NAME_DEFAULT,
    .shmSize = 1024*1024,
    .shmRmQ  = 0
};

void print_usage(const param_t *pm, FILE *s)
{
    fprintf(s, "Usage:\n");
    fprintf(s, "      -d shmRmQ [%d]: Remove shared memory if already exist.\n", pm->shmRmQ);
    fprintf(s, "      -n shmName [\"%s\"]: Shared memory object name, system-wide.\n", pm->shmName);
    fprintf(s, "      -s shmSize [%zd]: Shared memory size in bytes.\n", pm->shmSize);
}

int main(int argc, char **argv)
{
    int shmfd;
    void *shmp;
    size_t pageSize, sz=0;
    param_t pm;
    int optC = 0;

    // parse switches
    memcpy(&pm, &paramDefault, sizeof(pm));
    while((optC = getopt(argc, argv, "dn:s:")) != -1) {
        switch(optC) {
        case 'd':
            pm.shmRmQ = 1;
            break;
        case 'n':
            pm.shmName = optarg;
            break;
        case 's':
            pm.shmSize = strtoull(optarg, NULL, 10);
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
    fprintf(stderr, "System pagesize = %zd bytes.\n", pageSize);
    if(pm.shmSize <= 0 || (sz = pm.shmSize % pageSize) > 0) {
        fprintf(stderr, "shmSize (%zd) should be multiple of pagesize (%zd).\n",
                pm.shmSize, pageSize);
        pm.shmSize += (pageSize - sz);
        fprintf(stderr, "Enlarge to %zd.\n", pm.shmSize);
    }
    shmfd = shm_create(pm.shmName, &shmp, pm.shmSize, pm.shmRmQ);
    if(shmfd<0 || shmp==NULL) return EXIT_FAILURE;

    uint32_t *p = (uint32_t*)shmp;
    for(uint32_t i=0;;i++) {
        *p = i;
        usleep(1);
    }

    close(shmfd);
    return EXIT_SUCCESS;
}
