# NetDAQ

Network based DAQ

# Internals

## IPC
  - ```ipcrm``` to clean up upon process faults.
### Linux
  - ```ipcs -lm``` show shm limits
  - ```lsipc``` # util-linux>=2.27 show information on IPC facilities currently employed in the system
### FreeBSD and macOS
  - ```getconf PAGE_SIZE```
  - ```ipcs -M``` or ```-T``` Display system information about shared memory.
  - ```sysctl``` and ```/etc/sysctl.conf```
    - ```kern.ipc.shmall``` Maximum number of pages (normally 4096 bytes) available for shared memory
    - ```kern.ipc.shmmax``` Maximum shared memory segment size in bytes
    - macOS seems to name the parameters ```kern.sysv.```

# Coding style

The coding style used by this project is enforced with clang-format
using the configuration contained in the .clang-format file in the
root of the repository.

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
