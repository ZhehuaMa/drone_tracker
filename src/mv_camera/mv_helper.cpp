#include "mv_camera_base.h"
#include <NvBuffer.h>
#include <memory>

std::shared_ptr<NvBuffer> MVCameraBase::create_nv_buffer(uint8_t *y_ptr, uint8_t *uv_ptr, uint32_t w, uint32_t h, uint32_t stride)
{
    NvBuffer::NvBufferPlaneFormat plane_fmts[MAX_PLANES];
    plane_fmts[0].width = w;
    plane_fmts[0].height = h;
    plane_fmts[0].bytesperpixel = 1; // for V4L2_PIX_FMT_GREY
    plane_fmts[0].stride = stride;
    plane_fmts[0].sizeimage = stride * h;

    uint32_t uv_size = stride * h / 2;

    plane_fmts[1].width = w / 2;
    plane_fmts[1].height = h / 2;
    plane_fmts[1].bytesperpixel = 1; // for U and V planes
    plane_fmts[1].stride = stride / 2;
    plane_fmts[1].sizeimage = uv_size / 2;

    plane_fmts[2].width = w / 2;
    plane_fmts[2].height = h / 2;
    plane_fmts[2].bytesperpixel = 1; // for U and V planes
    plane_fmts[2].stride = stride / 2;
    plane_fmts[2].sizeimage = uv_size / 2;

    auto nv_buffer = std::make_shared<NvBuffer>(
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
        V4L2_MEMORY_USERPTR,
        3, plane_fmts, 0);
    
    nv_buffer->planes[0].data = y_ptr;
    nv_buffer->planes[0].bytesused = stride * h;
    nv_buffer->planes[0].mem_offset = 0;
    nv_buffer->planes[0].length = stride * h;

    nv_buffer->planes[1].data = reinterpret_cast<uint8_t *>(uv_ptr);
    nv_buffer->planes[1].bytesused = uv_size / 2;
    nv_buffer->planes[1].mem_offset = 0;
    nv_buffer->planes[1].length = uv_size / 2;

    nv_buffer->planes[2].data = reinterpret_cast<uint8_t *>(uv_ptr) + uv_size / 2;
    nv_buffer->planes[2].bytesused = uv_size / 2;
    nv_buffer->planes[2].mem_offset = 0;
    nv_buffer->planes[2].length = uv_size / 2;

    return nv_buffer;
}