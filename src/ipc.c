#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ipc.h"

/** System page size in bytes. */
size_t get_system_pagesize(void)
{
    return (size_t)sysconf(_SC_PAGESIZE);
}
/* Declaration to make C99/C11 happy. */
int ftruncate(int fd, off_t length);
/** Create shared memory. */
int shm_create(const char *name, void **p, size_t *size, shm_sync_t **ssv, int removeQ)
{
    int shmfd; // shared memory file descriptor.
    size_t esz;
    const mode_t mode = 0640; // rw-r-----
    uint8_t *p1;
    atomic_flag flag = ATOMIC_FLAG_INIT;

    assert(sizeof(shm_sync_t) <= get_system_pagesize());
    // Enlarged size.  Last page is for sync variables.
    esz = *size + SHM_SYNC_NPAGE*get_system_pagesize();

    if ((shmfd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, mode))<0) {
        fprintf(stderr, "Error in shm_open(\"%s\", ...): ", name);
        perror(NULL);
        if (errno == EEXIST && removeQ) {
            fprintf(stderr, "Removing shm \"%s\"...\n", name);
            shm_unlink(name);
        }
        return -1;
    }
    if (ftruncate(shmfd, esz) < 0) {
        fprintf(stderr, "Error in ftruncate() shm \"%s\" to size %zd: ", name, esz);
        perror(NULL);
        close(shmfd);
        return -1;
    }
    *p = mmap(NULL, esz, PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0);
    if (*p == NULL) {
        perror("mmap");
        close(shmfd);
        return -1;
    }
    p1 = (uint8_t*)(*p);
    if (ssv) {
        *ssv = (shm_sync_t*)(p1 + *size);
        (*ssv)->ovRun = flag;
    }
    *size = esz;
    return shmfd;
}
/** Connect to shared memory. */
int shm_connect(const char *name, void **p, size_t *size, shm_sync_t **ssv)
{
    int shmfd; // shared memory file descriptor.
    const mode_t mode = 0640; // rw-r-----
    struct stat sb;
    uint8_t *p1;

    assert(sizeof(shm_sync_t) <= get_system_pagesize());

    if ((shmfd = shm_open(name, O_RDWR, mode))<0) {
        fprintf(stderr, "Error in shm_open(\"%s\", ...): ", name);
        perror(NULL);
        return -1;
    }
    fstat(shmfd, &sb);
    if (size) {*size = (size_t)sb.st_size;}
    *p = mmap(NULL, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0);
    if (*p == NULL) {
        perror("mmap");
        close(shmfd);
        return -1;
    }
    p1 = (uint8_t*)(*p);
    if (ssv) { *ssv = (shm_sync_t*)(p1 + sb.st_size - SHM_SYNC_NPAGE*get_system_pagesize()); }
    return shmfd;
}
/** Acquire next segment for read/write, guarantee synchronicity. */
SHM_ELEM_TYPE *shm_acquire_next_segment_sync(const void *p, shm_sync_t *ssv, shm_seg_mode_t mode)
{
    SHM_ELEM_TYPE *rp;
    rp = (SHM_ELEM_TYPE*)p;
    intptr_t iRd, iRdTmp, iWr, iWrTmp, nSeg;

    if (mode == SHM_SEG_READ) {
        /* iWr is allowed to be more than +1 ahead of iRd, so two
         * separate atomic_load of iRd and iWr are allowed. */
        iRd = atomic_load(&ssv->iRd); // Owned by this consumer.
        /* Check if current segment is being written to. */
        iRdTmp = iRd;
        /* iRdTmp is overwritten if not equal. */
        if (atomic_compare_exchange_strong(&ssv->iWr, &iRdTmp, iRdTmp)) {
            return NULL;
        }
        /* Next segment is being written to. */
        iRd++;
        if (iRd == ssv->nSeg) { iRd = 0; } // Wrap around.
        iRdTmp = iRd;
        if (atomic_compare_exchange_strong(&ssv->iWr, &iRdTmp, iRdTmp)) {
            return NULL;
        }
        // Single consumer is assumed.  OK to atomic_store iRd after some calculations.
        atomic_store(&ssv->iRd, iRd);
        rp += ssv->segLen * iRd;
    } else if (mode == SHM_SEG_WRITE) {
        iWr = atomic_load(&ssv->iWr); // Owned by this producer.
        iWr++;
        if (iWr == ssv->nSeg) { iWr = 0; }
        /* Data ovRun: write is catching up to read. */
        iWrTmp = iWr;
        if (atomic_compare_exchange_strong(&ssv->iRd, &iWrTmp, iWrTmp)) {
            if (!atomic_flag_test_and_set(&ssv->ovRun)) { // Edge trigger.
                fprintf(stderr, "Data overrun.\n");
            }
        }
        /* Update rp and ssv->iWr. */
        nSeg = ssv->nSeg-1;
        /* nSeg is overwritten if not equal. */
        if (atomic_compare_exchange_strong(&ssv->iWr, &nSeg, 0)) {
            return rp;
        }
        iWr = atomic_fetch_add(&ssv->iWr, 1);
        rp += ssv->segLen * (iWr+1);
    }

    return rp;
}
/** Acquire oldest segment for read.  Does not guarantee synchronicity. */
SHM_ELEM_TYPE *shm_acquire_oldest_segment(const void *p, const shm_sync_t *ssv)
{
    SHM_ELEM_TYPE *rp;
    rp = (SHM_ELEM_TYPE*)p;
    intptr_t iRd, iWr;

    /* Segment currently being written to. */
    iWr = atomic_load(&ssv->iWr);
    if (iWr == 0)
        iRd = ssv->nSeg-1;
    else
        iRd = iWr-1;
    rp += ssv->segLen * iRd;
    return rp;
}
/** Update write counts: bytes and segs.  These variables are supposed
 * to be written in one transaction, but this implementation does not
 * guarantee that.  However, since the variables are for non-critical
 * performance monitoring, this is likely sufficiently good. */
void shm_update_write_count(shm_sync_t *ssv, size_t byteInc, size_t segInc)
{
    atomic_fetch_add(&ssv->wrBytes, byteInc);
    atomic_fetch_add(&ssv->wrSegs, segInc);
}
/** Get write counts: bytes and segs. */
size_t shm_get_write_count(shm_sync_t *ssv, size_t *byte, size_t *seg)
{
    size_t b, s;
    b = atomic_load(&ssv->wrBytes);
    s = atomic_load(&ssv->wrSegs);
    if (byte) { *byte = b; }
    if (seg) { *seg = s; }
    return b;
}
