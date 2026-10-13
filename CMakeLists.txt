cmake_minimum_required(VERSION 3.22.1)
project(malitools CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(malivkshim SHARED
    src/mali_vk_shim.cpp
)

target_include_directories(malivkshim PRIVATE
    include
)

target_link_libraries(malivkshim PRIVATE
    log
    dl
)

add_executable(test_shim
    src/test_mali_shim.cpp
)

target_include_directories(test_shim PRIVATE
    include
)

target_link_libraries(test_shim PRIVATE
    dl
)

