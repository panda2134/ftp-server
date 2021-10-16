cmake_minimum_required(VERSION 3.20)
project(server C)

set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
add_executable(server main.c server.c command.h client.h client.c command.c utils.c utils.h)