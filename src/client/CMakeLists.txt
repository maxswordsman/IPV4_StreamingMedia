cmake_minimum_required(VERSION 3.16)
project(client C)

set(CMAKE_C_STANDARD 99)
# 包含头文件
include_directories(../include)

add_executable(client client.c)