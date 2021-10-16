#ifndef FTP_SERVER_GLOBAL_H
#define FTP_SERVER_GLOBAL_H

#include <linux/limits.h>

#define MAX_USERNAME 32
#define FILENAME_SIZE 256
#define BUF_SIZE 8192 // only take last 8192 chars per request
#define USERNAME_LENGTH 32
#define INTERFACE_PREFIX "lo"

#include <stdlib.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "server.h"

#endif //FTP_SERVER_GLOBAL_H
