cmake_minimum_required(VERSION 3.5)
project(image-capture)
set(CMAKE_CXX_STANDARD 11)

set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_SOURCE_DIR}/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-g++")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d")

set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/host-tools/opencv-mobile-4.10.0-milkv-duo/lib/cmake/opencv4")
find_package(OpenCV REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/host-tools/wiringx)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)

add_executable(image-capture main/main.cpp)

target_link_libraries(image-capture ${OpenCV_LIBS} wiringx)