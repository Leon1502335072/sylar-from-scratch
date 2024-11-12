# 要求Cmake的最低版本
cmake_minimum_required(VERSION 3.0)
# 项目名称
project(sylar-from-scratch)

include (cmake/utils.cmake)

# 显示详细的生成日志，默认是OFF
set(CMAKE_VERBOSE_MAKEFILE ON)

# 指定编译选项 $ENV{}用于获取环境变量设置环境变量的方法 CXXFLAGS c++编译器的选项，无默认值
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -std=c++11 -O0 -ggdb -Wall -Werror")

# -rdynamic: 将所有符号都加入到符号表中，便于使用dlopen或者backtrace追踪到符号
# -fPIC: 生成位置无关的代码，便于动态链接
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -rdynamic -fPIC")

# -Wno-unused-function: 不要警告未使用函数
# -Wno-builtin-macro-redefined: 不要警告内置宏重定义，用于重定义内置的__FILE__宏
# -Wno-deprecated: 不要警告过时的特性
# -Wno-deprecated-declarations: 不要警告使用带deprecated属性的变量，类型，函数
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function -Wno-builtin-macro-redefined -Wno-deprecated -Wno-deprecated-declarations")

# 将指定目录添加到编译器的头文件搜索路径之下，指定的目录被解释成当前源码路径的相对路径
# 该命令就是在所有的源码.cpp文件在查找头文件的时候都会到当前目录去寻找
include_directories(.)

# 定义一个BUILD_TEST变量，描述信息，开启该变量
option(BUILD_TEST "ON for complile test" ON)

# 查找boost库 使用REQUIRED选项后，如果包没有被找到，就会产生一个错误信息，中断处理
find_package(Boost REQUIRED) 
if(Boost_FOUND) # 包名boost+_FOUND 用于判断是否查找成功 Boost_INCLUDE_DIRS是boost库的头文件的路径
    include_directories(${Boost_INCLUDE_DIRS})
endif()

set(LIB_SRC
    sylar/log.cpp
    sylar/util.cpp
    sylar/mutex.cc
    sylar/env.cc
    sylar/config.cc
    sylar/thread.cc
    sylar/fiber.cc
    sylar/scheduler.cc
    sylar/iomanager.cc
    sylar/timer.cc
    sylar/fd_manager.cc
    sylar/hook.cc
    sylar/address.cc 
    sylar/socket.cc 
    sylar/bytearray.cc 
    sylar/tcp_server.cc 
    sylar/http/http-parser/http_parser.c 
    sylar/http/http.cc
    sylar/http/http_parser.cc 
    sylar/stream.cc 
    sylar/streams/socket_stream.cc
    sylar/http/http_session.cc 
    sylar/http/servlet.cc
    sylar/http/http_server.cc 
    sylar/uri.cc 
    sylar/http/http_connection.cc 
    sylar/daemon.cc 
    )

add_library(sylar SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(sylar)

set(LIBS
    sylar
    pthread
    dl
    yaml-cpp
)

if(BUILD_TEST)
sylar_add_executable(test_log "tests/test_log.cpp" sylar "${LIBS}")
sylar_add_executable(test_util "tests/test_util.cpp" sylar "${LIBS}")
sylar_add_executable(test_env "tests/test_env.cc" sylar "${LIBS}")
sylar_add_executable(test_config "tests/test_config.cc" sylar "${LIBS}")
sylar_add_executable(test_thread "tests/test_thread.cc" sylar "${LIBS}")
sylar_add_executable(test_fiber "tests/test_fiber.cc" sylar "${LIBS}")
sylar_add_executable(test_fiber2 "tests/test_fiber2.cc" sylar "${LIBS}")
sylar_add_executable(test_scheduler "tests/test_scheduler.cc" sylar "${LIBS}")
sylar_add_executable(test_iomanager "tests/test_iomanager.cc" sylar "${LIBS}")
sylar_add_executable(test_timer "tests/test_timer.cc" sylar "${LIBS}")
sylar_add_executable(test_hook "tests/test_hook.cc" sylar "${LIBS}")
sylar_add_executable(test_address "tests/test_address.cc" sylar "${LIBS}")
sylar_add_executable(test_socket_tcp_server "tests/test_socket_tcp_server.cc" sylar "${LIBS}")
sylar_add_executable(test_socket_tcp_client "tests/test_socket_tcp_client.cc" sylar "${LIBS}")
sylar_add_executable(test_bytearray "tests/test_bytearray.cc" sylar "${LIBS}")
sylar_add_executable(test_tcp_server "tests/test_tcp_server.cc" sylar "${LIBS}")
sylar_add_executable(test_http "tests/test_http.cc" sylar "${LIBS}")
sylar_add_executable(test_http_parser "tests/test_http_parser.cc" sylar "${LIBS}")
sylar_add_executable(test_http_server "tests/test_http_server.cc" sylar "${LIBS}")
sylar_add_executable(test_uri "tests/test_uri.cc" sylar "${LIBS}")
sylar_add_executable(test_http_connection "tests/test_http_connection.cc" sylar "${LIBS}")
sylar_add_executable(test_daemon "tests/test_daemon.cc" sylar "${LIBS}")
sylar_add_executable(test_myhttpserver "tests/test_myhttpserver.cc" sylar "${LIBS}")
endif()

# 指定可执行程序的生成路径 PROJECT_SOURCE_DIR这个宏就是cmake 命令后面的路径 也就是CMakeLists.txt所在的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
# 指定库的生成路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)