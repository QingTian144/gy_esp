cmake_minimum_required(VERSION 3.22.1)

project(gyesp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 体积与特征优化：隐藏符号、去掉无用段
set(CMAKE_CXX_FLAGS_RELEASE
    "-O2 -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "-Wl,--gc-sections -s")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../kernel
)

add_library(gyesp SHARED
    main.cpp
    mem.cpp
    kdrv.cpp
    ue4.cpp
    dump.cpp
    esp.cpp
    overlay.cpp
)

find_library(log-lib     log)
find_library(android-lib android)
find_library(EGL-lib     EGL)
find_library(GLES-lib    GLESv2)

target_link_libraries(gyesp
    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLES-lib}
)

target_link_options(gyesp PRIVATE "-Wl,-z,max-page-size=16384")
