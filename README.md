# NetDAQ

Network based DAQ.

# Internals

A circular buffer, implemented with shared memory (shm) between processes, is used to temporarily store data received by ```ndrecv```.  A single producer (```ndrecv```) is allowed to write to the shm.  Consumer ```ndsave``` is responsible for reading data from shm and save to file.  Between ```ndsave``` and ```ndrecv``` are synchronized through a lock-free mechanism.  ```ndsave``` waits on ```ndrecv``` to produce sufficient data.  If ```ndsave``` is slower in consuming data than ```ndrecv```, this race condition is detected and reported but the old data is overwritten regardless.  Other consumers, or better named `spectators' such as ```nddisp```, are allowed to access shm provided that they do not disturb the synchronization mechanism.

The shm is divided into segments.  The producer and the consumer each acquire and operate on one single segment at a time.  Spectators are advised to check ```shm_sync_t->iWr``` to avoid reading the segment that is being written to.

```shm_sync_t``` is stored at the last page of the shm.

## IPC
  - ```ipcrm``` to clean up upon process faults.
### Linux
  - ```ipcs -lm``` to show shm limits.
  - ```lsipc``` ```# util-linux>=2.27``` to show information on IPC facilities currently employed in the system.
  - ```pmap -x PID``` to view memory mapping.
### FreeBSD and macOS
  - ```getconf PAGE_SIZE```
  - ```ipcs -M``` or ```-T``` to display system information about shared memory.
  - ```sysctl``` and ```/etc/sysctl.conf```
    - ```kern.ipc.shmall``` Maximum number of pages (normally 4096 bytes) available for shared memory.
    - ```kern.ipc.shmmax``` Maximum shared memory segment size in bytes.
    - macOS seems to name the parameters ```kern.sysv.```
  - ```procstat -v PID``` to view memory mapping on FreeBSD.
  - ```vmmap -v PID``` to view memory mapping on macOS.

# Coding style

The coding style used by this project is enforced with clang-format using the configuration contained in the .clang-format file in the root of the repository.

Before pushing changes you can format your code with:

```
# Apply clang-format to modified .h, .c and .cpp files
$ clang-format -i -style=file $(git diff --name-only --diff-filter=ACMR '*.[hc]' '*.cpp')
```

The .clang-format file is generated using the command
```clang-format -style=Google -dump-config > .clang-format```

See also: http://clang.llvm.org/docs/ClangFormat.html

## Doxygen

Remember to update tag files under ```doc/doxytag/``` often.  They are referenced in ```Doxyfile```.

# Git usage policy

We adopt the [Feature Branch Workflow](https://www.atlassian.com/git/tutorials/comparing-workflows/feature-branch-workflow).  Every contributor shall create and work on her/his own development/feature branch.  Newly developed code in such a branch shall be reviewed by initiating a pull request.

The ```master``` branch shall never contain broken code.  Only vetted code (via pull requests) can be committed to the ```master``` branch.

[GPG signing](https://confluence.atlassian.com/bitbucketserver/using-gpg-keys-913477014.html) of commits are highly encouraged.
