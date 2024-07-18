cmake_minimum_required(VERSION 3.16)
project(server C)

set(CMAKE_C_STANDARD 99)
include_directories(../include)

add_executable(server server.c mytbf.c thr_channel.c thr_list.c medialib.c)

# 直接链接pthread到你的项目
target_link_libraries(server PRIVATE pthread)