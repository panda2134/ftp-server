//
// Created by panda2134 on 2021/10/15.
//

#include <stdio.h>
#include <stdlib.h>
#include <ifaddrs.h>
#include <string.h>
#include <stdbool.h>

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