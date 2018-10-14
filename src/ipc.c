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
/** Create shared memory */
int shm_create(const char *name, void **p, size_t size, int removeQ)
{
    int shmfd; // shared memory file descriptor.
    const mode_t mode = 0640; // rw-r-----
    size += IPC_SYNC_PAGES*get_system_pagesize(); // enlarge size.  Last page is for sync variables.

    if((shmfd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, mode))<0) {
        fprintf(stderr, "Error in shm_open(\"%s\", ...): ", name);
        perror(NULL);
        if(errno == EEXIST && removeQ) {
            fprintf(stderr, "Removing shm \"%s\"...\n", name);
            shm_unlink(name);
        }
        return -1;
    }
    if(ftruncate(shmfd, size) < 0) {
        fprintf(stderr, "Error in ftruncate() shm \"%s\" to size %zd: ", name, size);
        perror(NULL);
        return -1;
    }
    *p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0);
    if(*p == NULL) {
        perror("mmap");
        close(shmfd);
        return -1;
    }
    return shmfd;
}
/** Connect to shared memory. */
int shm_connect(const char *name, void **p, size_t *size)
{
    int shmfd; // shared memory file descriptor.
    const mode_t mode = 0640; // rw-r-----
    struct stat sb;

    if((shmfd = shm_open(name, O_RDWR, mode))<0) {
        fprintf(stderr, "Error in shm_open(\"%s\", ...): ", name);
        perror(NULL);
        return -1;
    }
    fstat(shmfd, &sb);
    if(size) {*size = (size_t)sb.st_size;}
    *p = mmap(NULL, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0);
    if(*p == NULL) {
        perror("mmap");
        close(shmfd);
        return -1;
    }
    return shmfd;
}
