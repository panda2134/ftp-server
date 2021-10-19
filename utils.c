//
// Created by panda2134 on 2021/10/15.
//

#include "server.h"
#include "utils.h"
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <ifaddrs.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

void encode_pwd(const char* pathname, char* dest, size_t* result_len) {
  char* original_dest = dest;
  *(dest++) = '"';
  for (; *pathname; pathname++, dest++) {
    if (*pathname == '\012') {
      *dest = '\0';
    } else if (*pathname == '"') {
      *(dest++) = '"';
      *dest = '"';
    } else {
      *dest = *pathname;
    }
  }
  *(dest++) = '"';
  *result_len = dest - original_dest;
}

const char* decode_pathname(const char* encoded, size_t length) {
  static char buf[PATH_MAX];
  if (length + 1 >= PATH_MAX) { // consider \0
    return NULL;
  }
  memcpy(buf, encoded, length);
  for (int i = 0; i < length; i++) {
    if (buf[i] == '\0') buf[i] = '\012';
  }
  buf[length] = '\0';
  return buf;
}

struct sockaddr* get_first_inet_addr_with_prefix(const char* prefix) {
  struct ifaddrs * ifaddr;
  if (getifaddrs(&ifaddr) == -1) {
    perror("getifaddr()");
    return NULL;
  }
  size_t prefix_len = strlen(prefix);
  bool found = false;
  struct sockaddr* ad = malloc(sizeof(struct sockaddr));
  for (struct ifaddrs * ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) continue;
    if (strncmp(prefix, ifa->ifa_name, prefix_len) != 0) continue;
    memcpy(ad, ifa->ifa_addr, sizeof(struct sockaddr));
    found = true;
    break;
  }
  freeifaddrs(ifaddr);
  return found ? ad : NULL;
}

char* eplf_line(const char* filename, struct stat *stat_info, size_t *return_len) {
  static char result[BUF_SIZE], buf[BUF_SIZE]; // should be in all-zero
  strcpy(result, "+");
  // parse identifier
  sprintf(buf, "i%lu.%lu,", stat_info->st_dev, stat_info->st_ino);
  strcat(result, buf);
  // parse last-modified time
  sprintf(buf, "m%lu,", stat_info->st_mtim.tv_sec);
  strcat(result, buf);
  // parse /
  if (S_ISDIR(stat_info->st_mode)) {
    strcat(result, "/,");
  }
  // parse r
  if (S_ISREG(stat_info->st_mode)) {
    strcat(result, "r,");
  }
  // parse size
  sprintf(buf, "s%lu,", stat_info->st_size);
  strcat(result, buf);
  // filename
  strcat(result, "\t");
  size_t i, prefix_len = strlen(result);
  for (i = 0; filename[i]; i++) {
    if (filename[i] == '\012')
      result[i + prefix_len] = '\0';
    else
      result[i + prefix_len] = filename[i];
  }
  *return_len = i + prefix_len;
  return result;
}

bool is_valid_path(ftp_server_t *server, const char *path) {
  char basepath_buf[PATH_MAX], path_buf[PATH_MAX], path_buf_alt[PATH_MAX];
  if (realpath(server->basepath, basepath_buf) == NULL) {
    perror("resolving basepath in is_valid_path()");
    return false;
  }
  if (realpath(path, path_buf) == NULL) {
    // take away the last part, and try again...
    if (errno != ENOENT) {
      perror("resolving path in is_valid_path()");
      return false;
    }
    ssize_t slash_position = 0;
    for (int i = 0; path[i]; i++) {
      if (path[i] == '/') slash_position = i;
    }
    memcpy(path_buf_alt, path, slash_position);
    path_buf_alt[slash_position] = '\0';
    if (realpath(path_buf_alt, path_buf) == NULL) {
      perror("2nd resolving path in is_valid_path()");
      return false;
    }
  }
  return strncmp(basepath_buf, path_buf, strlen(basepath_buf)) == 0;
}

const char * resolve_path_to_host(const char* basepath, const char* cwd, const char* pathname) {
  static char buf[PATH_MAX];
  if (pathname[0] == '/') {
    strncpy(buf, basepath, PATH_MAX);
  } else {
    strncpy(buf, cwd, PATH_MAX);
    strncat(buf, "/", PATH_MAX-1);
  }
  strncat(buf, pathname, PATH_MAX);
  return buf;
}