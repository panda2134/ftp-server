#ifndef FTP_SERVER_SERVER_H
#define FTP_SERVER_SERVER_H

#include <sys/epoll.h>
#include <dirent.h>
#include "global.h"
#include "client.h"

#define MAX_CLIENT 65536
#define MAX_EVENTS 131072

typedef struct ftp_server_t {
  struct epoll_event events[MAX_EVENTS];
  int num_event, epollfd, cntl_listen_fd;
  int num_client, num_closed;
  struct sockaddr_in listen_addr;
  char basepath[PATH_MAX];
  struct ftp_client_t * closed[MAX_CLIENT];
} ftp_server_t;

ftp_server_t* create_ftp_server(int cntl_port, const char* basepath, const char* if_prefix);

_Noreturn void server_loop(ftp_server_t* server);

#endif