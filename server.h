#ifndef FTP_SERVER_SERVER_H
#define FTP_SERVER_SERVER_H

#include <sys/epoll.h>
#include <dirent.h>
#include "hashtable.h"
#include "global.h"
#include "client.h"

#define MAX_CLIENT 65536
#define MAX_EVENTS 131072

typedef struct ftp_server_t {
  struct epoll_event events[MAX_EVENTS];
  int num_event, epollfd, cntl_listen_fd;
  int num_client;
  char basepath[PATH_MAX];
} ftp_server_t;

ftp_server_t* create_ftp_server(int cntl_port, const char* basepath);
bool is_valid_path(ftp_server_t* server, const char* path);
_Noreturn void server_loop(ftp_server_t* server);

#endif