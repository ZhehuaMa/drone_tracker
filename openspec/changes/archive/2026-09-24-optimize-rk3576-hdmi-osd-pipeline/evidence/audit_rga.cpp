#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <im2d.hpp>
namespace {
std::atomic<unsigned> operations{}, copies{}, blends{}, commits{};
bool suppress_event{};
const char *fault() { const auto *value{std::getenv("OSD_TEST_FAULT")}; return value ? value : ""; }
}
IM_STATUS improcess(rga_buffer_t src, rga_buffer_t dst, rga_buffer_t pat,
                    im_rect sr, im_rect dr, im_rect pr, int acquire, int *release, im_opt_t *opt, int usage) {
    using Function = IM_STATUS (*)(rga_buffer_t, rga_buffer_t, rga_buffer_t, im_rect, im_rect, im_rect,
                                   int, int *, im_opt_t *, int);
    const auto real{reinterpret_cast<Function>(dlsym(RTLD_NEXT, "_Z9improcess12rga_buffer_tS_S_7im_rectS0_S0_iPiP6im_opti"))};
    ++operations;
    return real(src,dst,pat,sr,dr,pr,acquire,release,opt,usage);
}
IM_STATUS imcopy(rga_buffer_t src, rga_buffer_t dst, int sync, int *release) {
    using Function = IM_STATUS (*)(rga_buffer_t,rga_buffer_t,int,int *);
    const auto real{reinterpret_cast<Function>(dlsym(RTLD_NEXT,"_Z6imcopy12rga_buffer_tS_iPi"))};
    ++copies; return real(src,dst,sync,release);
}
IM_STATUS imblend(rga_buffer_t src, rga_buffer_t dst, int mode, int sync, int *release) {
    using Function = IM_STATUS (*)(rga_buffer_t,rga_buffer_t,int,int,int *);
    const auto real{reinterpret_cast<Function>(dlsym(RTLD_NEXT,"_Z7imblend12rga_buffer_tS_iiPi"))};
    ++blends; return real(src,dst,mode,sync,release);
}
extern "C" int drmModeAtomicCommit(int fd, void *request, unsigned flags, void *data) {
    using Function = int (*)(int,void *,unsigned,void *);
    const auto real{reinterpret_cast<Function>(dlsym(RTLD_NEXT,"drmModeAtomicCommit"))};
    if ((flags & 0x200) && ++commits == 30) {
        if (std::strcmp(fault(),"commit") == 0) { std::fputs("OSD_TEST atomic EIO\n",stderr); errno=EIO; return -1; }
        if (std::strcmp(fault(),"timeout") == 0) { std::fputs("OSD_TEST suppress flip event\n",stderr); suppress_event=true; }
    }
    return real(fd,request,flags,data);
}
extern "C" int drmHandleEvent(int fd, void *context) {
    using Function = int (*)(int,void *);
    const auto real{reinterpret_cast<Function>(dlsym(RTLD_NEXT,"drmHandleEvent"))};
    if (suppress_event) { return 0; }
    return real(fd,context);
}
__attribute__((destructor)) void report() {
    std::fprintf(stderr,"OSD_RGA_AUDIT operations=%u copies=%u blends=%u commits=%u\n",
                 operations.load(),copies.load(),blends.load(),commits.load());
}
