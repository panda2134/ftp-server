//
// Created by panda2134 on 2021/10/15.
//

#ifndef FTP_SERVER_UTILS_H
#define FTP_SERVER_UTILS_H

#include <sys/epoll.h>
#include <sys/stat.h>
#include <dirent.h>
#include "global.h"
#include "client.h"

/**
 * encode the current directory string into a form that's suitable for ftp
 * @param pathname the original pathname terminated by \000
 * @param dest destination of the encoded bytes
 * @param len length of encoded bytes
 */
void encode_pwd(const char* pathname, char* dest, size_t* result_len);


/**
 * get the first ip address whose the device name starts with a given prefix.
 * @param prefix the given prefix
 * @return pointer to the required sockaddr, NULL if not found or an error occured
 * @note the pointer returned is dynamically allocated, and should be freed with `free()` when no longer used.
 */
struct sockaddr* get_first_inet_addr_with_prefix(const char* prefix);

/**
 * generate EPLF line for stat info
 * @param stat_info stat information of inode
 * @return eplf status line. it is statically allocated in a shared buffer, and shall not be freed.
 */
char* eplf_line(const char* filename, struct stat *stat_info);

/**
 * check if the given path is
 *  1. inside the server basepath
 *  2. valid / valid after removing its last slice
 * @param server
 * @param path
 * @return
 */
bool is_valid_path(ftp_server_t* server, const char* path);

/**
 * resolve the path argument in FTP commands to a valid path (can contain ./..) in the host's filesystem.
 * @param basepath "root" for the FTP commands
 * @param cwd absolute path of current directory
 * @param pathname a path in FTP commands
 * @return resolved path. it is statically allocated in a buffer, so never call free() on it.
 */
const char * resolve_path_to_host(const char* basepath, const char* cwd, const char* pathname);

#endif //FTP_SERVER_UTILS_H
