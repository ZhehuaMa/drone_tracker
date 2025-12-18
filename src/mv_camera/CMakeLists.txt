cmake_minimum_required(VERSION 3.20)
project(mv_camera)

find_package(VPI 3.2 REQUIRED)
find_package(CUDAToolkit QUIET)

set(JETSON_MULTIMEDIA_API_PATH /usr/src/jetson_multimedia_api)
set(JETSON_COMMON_PATH ${JETSON_MULTIMEDIA_API_PATH}/samples/common/classes)
set(COMPONENT_FILES
    ${JETSON_COMMON_PATH}/NvJpegDecoder.cpp
    ${JETSON_COMMON_PATH}/NvJpegEncoder.cpp
    ${JETSON_COMMON_PATH}/NvBuffer.cpp
    ${JETSON_COMMON_PATH}/NvLogging.cpp
    ${JETSON_COMMON_PATH}/NvUtils.cpp
    ${JETSON_COMMON_PATH}/NvV4l2Element.cpp
    ${JETSON_COMMON_PATH}/NvV4l2ElementPlane.cpp
    ${JETSON_COMMON_PATH}/NvElement.cpp
    ${JETSON_COMMON_PATH}/NvElementProfiler.cpp
)

add_library(${PROJECT_NAME} STATIC
    mjpeg_writer.cpp
    mv_camera.cpp
    mv_video.cpp
    mv_helper.cpp
    ${COMPONENT_FILES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${JETSON_MULTIMEDIA_API_PATH}/include
    ${JETSON_MULTIMEDIA_API_PATH}/include/libjpeg-8b
)

target_link_directories(${PROJECT_NAME} PUBLIC
    /usr/lib/aarch64-linux-gnu
    /usr/lib/aarch64-linux-gnu/tegra
)

target_link_libraries(${PROJECT_NAME} 
    ${OpenCV_LIBS} 
    vpi
    CUDA::cudart
    nvv4l2 nvbufsurface nvbufsurftransform nvjpeg
    avformat avcodec avutil)



