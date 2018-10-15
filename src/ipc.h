/** \file ipc.h
 * Utilities for inter-process communication, mainly shared memory.
 */
#ifndef __IPC_H__
#define __IPC_H__

#include <stdatomic.h>
#include <stdint.h>
#include "common.h"

/** Number of pages for synchronization variables. */
#define SHM_SYNC_NPAGE 1
/** Shared memory segment access modes. */
typedef enum shm_seg_mode {
    SHM_SEG_READ  = 0,
    SHM_SEG_WRITE = 1
} shm_seg_mode_t;
/** Variables for shm synchronization. */
typedef struct shm_sync
{
    size_t          elemSize;   //!< fundamental element size, e.g. 4 for uint32_t.
    size_t          segLen;     //!< segment length.  nBytes = segLen * elemSize.
    size_t          nSeg;       //!< number of segments.
    atomic_intptr_t iRd;        //!< index of segment being read.
    atomic_intptr_t iWr;        //!< index of segment being written to.
    atomic_flag     ovRun;      //!< flag indicating write overruns read.
    atomic_size_t   wrBytes;    //!< written bytes.
    atomic_size_t   wrSegs;     //!< written segments.
} shm_sync_t;
/** Initial values for shm_sync_t.  Called by producer only. */
#define shm_sync_producer_init(v)               \
    do {                                        \
        v->elemSize = sizeof(SHM_ELEM_TYPE);    \
        v->segLen   = SHM_SEG_LEN;              \
        v->nSeg     = SHM_NSEG;                 \
        atomic_init(&v->iRd, 0);                \
        atomic_init(&v->iWr, 0);                \
        atomic_flag_test_and_set(&v->ovRun);    \
        atomic_init(&v->wrBytes, 0);            \
        atomic_init(&v->wrSegs,  0);            \
    } while(0);
/** Initialize for consumer.  Data overrun check starts by this. */
#define shm_sync_consumer_init(v)               \
    do {                                        \
        atomic_store(&v->iRd, v->iWr);          \
        atomic_flag_clear(&v->ovRun);           \
    } while(0);

/** System page size.
 * @return pagesize in bytes
 */
size_t get_system_pagesize(void);
/** Create shared memory.
 * @param[out] p memory address (mmap).
 * @param[inout] size in: requested shm data size; out: enlarged by adding IPC_SYNC_PAGES
 *                    page to store synchronization variables.
 * @param[out] ssv pointer to synchronization variables structure.
 * @param[in] removeQ shm_unlink the file if exist, then return failure immediately.
 * @return shmfd.  Should close() after use.  -1 on failure.
 */
int shm_create(const char *name, void **p, size_t *size, shm_sync_t **ssv, int removeQ);
/** Connect to shared memory.
 * @param[out] p memory address (mmap).
 * @param[out] size shm size in bytes, including the page containing synchronization variables.
 * @param[out] ssv pointer to synchronization variables structure.
 * @return shmfd.  Should close() after use.  -1 on failure.
 */
int shm_connect(const char *name, void **p, size_t *size, shm_sync_t **ssv);
/** Acquire next segment for read/write synchronously.
 * Segments are supplied circularly.  It is assumed that only one producer writes to shm.
 * Read counter iRd is modified.  It is assumed that only one consumer reads synchronously.
 * @param[in] p pointer to mmap-ed shared memory.
 * @param[in] ssv pointer to shm_sync_t.
 * @return For read, NULL if iRd == iWr-1.
 */
SHM_ELEM_TYPE *shm_acquire_next_segment_sync(const void *p, shm_sync_t *ssv, shm_seg_mode_t mode);
/** Update write counts: bytes and segs
 * @param[in] ssv pointer to shm_sync_t.
 */
void shm_update_write_count(shm_sync_t *ssv, size_t byteInc, size_t segInc);
/** Get write counts: bytes and segs.
 * @param[out] byte bytes written if not NULL.
 * @param[out] seg segments written if not NULL.
 * @return bytes written.
 */
size_t shm_get_write_count(shm_sync_t *ssv, size_t *byte, size_t *seg);

#endif /* __IPC_H__ */
