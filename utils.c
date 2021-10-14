//
// Created by panda2134 on 2021/10/15.
//

#include <stdlib.h>
#include <string.h>
#include <limits.h>

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