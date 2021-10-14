cmake_minimum_required(VERSION 3.20)
project(ftp_server C)

set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
add_executable(ftp_server main.c server.c hashtable.c command.h hashtable.h client.h client.c command.c utils.c utils.h)