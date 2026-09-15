if(DRONE_TRACKER_PLATFORM STREQUAL "rk3576")
    add_subdirectory(rk3576 production)
    return()
endif()
# 仅由已选择 Jetson 的生产构建进入；公共算法不反向依赖本目标。
set(TRACKER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../hybrid_tracker)
add_library(platform_jetson STATIC
    jetson/platform_factory.cpp
    ${TRACKER_DIR}/model/jetson/tensor_buffer.cpp
    ${TRACKER_DIR}/model/jetson/jetson_storage.cpp
    ${TRACKER_DIR}/model/jetson/tensor_rt_model_executor.cpp
    ${TRACKER_DIR}/model/jetson/jetson_model_factory.cpp
    ${TRACKER_DIR}/preprocess/jetson/jetson_model_input.cpp
    ${TRACKER_DIR}/preprocess/jetson/jetson_image_ops.cpp
    ${TRACKER_DIR}/preprocess/jetson/jetson_yolo_preprocessor.cpp
    ${TRACKER_DIR}/preprocess/jetson/jetson_nanotrack_preprocessor.cpp
)
target_include_directories(platform_jetson PRIVATE ${TRACKER_DIR} ${TensorRT_INCLUDE_DIRS})
target_link_libraries(platform_jetson PUBLIC platform_interfaces jetson_tracker_assembly preprocess_geometry
    drone_frame mv_camera ${OpenCV_LIBS} nvinfer CUDA::cudart vpi Threads::Threads)
