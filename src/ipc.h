/** \file ipc.h
 * Utilities for inter-process communication, mainly shared memory.
 */
#ifndef __IPC_H__
#define __IPC_H__

/** Number of pages for synchronization variables */
#define IPC_SYNC_PAGES 1

/** System page size.
 * @return pagesize in bytes
 */
size_t get_system_pagesize(void);
/** Create shared memory.
 * @param[out] p memory address (mmap).
 * @param[in]  size is automatically enlarged by adding IPC_SYNC_PAGES page
               to store synchronization variables.
 * @return shmfd.  Should close() after use.  -1 on failure.
 */
int shm_create(const char *name, void **p, size_t size, int removeQ);
/** Connect to shared memory.
 * @param[out] p memory address (mmap).
 * @param[out] size in bytes, including the page containing synchronization variables.
 * @return shmfd.  Should close() after use.  -1 on failure.
 */
int shm_connect(const char *name, void **p, size_t *size);

#endif /* __IPC_H__ */
