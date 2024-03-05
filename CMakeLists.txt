cmake_minimum_required(VERSION 3.10)
project(QServer)


SET(ROOT_DIR ${CMAKE_SOURCE_DIR})


# Set C++ standard
set(CMAKE_CXX_STANDARD 11)

# 添加预定义宏
add_definitions(-DSERVER)


# Add include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/luajit/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/mysql/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/include/openssl
)

# Add source files
set(SOURCES 
    src/lz4.c
    src/power.cpp
    src/server.cpp
)

# Add executable target
add_executable(${PROJECT_NAME} ${SOURCES})


if(WIN32)
    target_link_options(${PROJECT_NAME} PRIVATE "/SAFESEH:NO")
endif()



set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${DEBUG_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${RELEASE_FLAGS}")

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/x86-release/navmeshd.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/x86-release/lua51.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/x86-release/mysqlclient.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/x86-release/libeay32.lib
    legacy_stdio_definitions.lib
    # ${CMAKE_CURRENT_SOURCE_DIR}/Library/zlib.lib
)

set(EXECUTABLE_OUTPUT_PATH ${ROOT_DIR}/bin)

