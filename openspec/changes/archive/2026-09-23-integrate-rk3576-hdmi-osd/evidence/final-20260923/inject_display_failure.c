/* 仅用于隔离验收进程：第 100 次非阻塞显示提交返回 EIO。 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
int drmModeAtomicCommit(int fd, void *request, uint32_t flags, void *data) {
    static int (*real_commit)(int, void *, uint32_t, void *);
    static unsigned count;
    if (!real_commit) real_commit = dlsym(RTLD_NEXT, "drmModeAtomicCommit");
    if ((flags & 0x0200) && ++count == 100) {
        fputs("OSD_TEST injected atomic EIO\n", stderr);
        errno = EIO;
        return -1;
    }
    return real_commit(fd, request, flags, data);
}
