//
// Created by panda2134 on 2021/10/15.
//

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

char* eplf_line(const char* filename, struct stat *stat_info) {
  static char result[BUFSIZ], buf[BUFSIZ]; // should be in all-zero
  strcpy(result, "+");
  // parse identifier
  sprintf(buf, "i%lu.%lu,", stat_info->st_dev, stat_info->st_ino);
  strcat(result, buf);
  // parse last-modified time
  sprintf(buf, "m%lu,", stat_info->st_mtim.tv_sec);
  strcat(result, buf);
  // parse size
  sprintf(buf, "s%lu,", stat_info->st_size);
  strcat(result, buf);
  // parse r
  if (S_ISREG(stat_info->st_mode)) {
    strcat(result, "r,");
  }
  // parse /
  if (S_ISDIR(stat_info->st_mode)) {
    strcat(result, "/,");
  }
  // filename
  strcat(result, " ");
  strcat(result, filename);
  return result;
}