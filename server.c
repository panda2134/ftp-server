#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include "server.h"

ftp_server_t* create_ftp_server(int cntl_port, const char* basepath) {
  ftp_server_t *server = (ftp_server_t*) malloc(sizeof(ftp_server_t));
  memset(server, 0, sizeof(ftp_server_t));

  /* initialize base path */
  realpath(basepath, server->basepath);
  if (errno) {
    perror("Unable to open server root directory");
    exit(EXIT_FAILURE);
  }

  /* initialize cntl */
  server->cntl_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server->cntl_listen_fd == -1) {
    perror("socket() of cntl_listen_fd"); exit(EXIT_FAILURE);
  }
  struct sockaddr_in cntl_addr;
  cntl_addr.sin_family = AF_INET;
  cntl_addr.sin_port = cntl_port;
  cntl_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(server->cntl_listen_fd, (struct sockaddr*)&cntl_addr, sizeof(cntl_addr)) == -1) {
    perror("bind() of cntl_listen_fd"); exit(EXIT_FAILURE);
  }
  if (listen(server->cntl_listen_fd, MAX_CLIENT) == -1) {
    perror("listen() of cntl_listen_fd"); exit(EXIT_FAILURE);
  }
  if (fcntl(server->cntl_listen_fd, F_SETFL, O_NONBLOCK) == -1) {
    perror("fcntl(): cannot set cntl_listen_fd to be nonblock"); exit(EXIT_FAILURE);
  }

  /* initialize epoll */
  server->epollfd = epoll_create1(0);
  if (server->epollfd == -1) {
    perror("epoll_create1"); exit(EXIT_FAILURE);
  }

  /* add cntl_sock to epoll */
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.ptr = NULL;
  if (epoll_ctl(server->epollfd, EPOLL_CTL_ADD, ev.data.fd, &ev) == -1) {
    perror("epoll_ctl for cntl_listen_fd"); exit(EXIT_FAILURE);
  }

  return server;
}

bool is_valid_path(ftp_server_t* server, const char* resolved_path) {
  size_t basepath_len = strlen(server->basepath);
  return strlen(resolved_path) >= strlen(server->basepath)
    && memcmp(resolved_path, server->basepath, basepath_len) == 0;
}

_Noreturn void server_loop(ftp_server_t* server) {
  while (true) {
      server->num_event = epoll_wait(server->epollfd, server->events, MAX_EVENTS, -1);
      if (server->num_event == -1) {
          perror("epoll_wait");
          exit(EXIT_FAILURE);
      }
      for (int i = 0; i < server->num_event; i++) {
        if (server->events[i].data.ptr == NULL) {
          // a connection to control port;
          ftp_client_t *client = malloc(sizeof(ftp_client_t));
          create_client(client, server);

#define CONTINUE_IF_FAIL(msg) if (errno) { perror(msg); close(client->cntl_fd); free(client); continue; }
          // STEP 1: try to accept
          client->cntl_fd = accept(server->cntl_listen_fd,
                                  (struct sockaddr*)&(client->cntl_sockaddr),
                                  &(client->cntl_sockaddr_len));
          CONTINUE_IF_FAIL("accept")

          // STEP 2: try to set non-blocking
          fcntl(client->cntl_fd, F_SETFL, O_NONBLOCK);
          CONTINUE_IF_FAIL("fcntl")

          // STEP 3: use epoll to watch the socket
          struct epoll_event ev;
          ev.events = EPOLLIN; // at S_CMD, wait for reading command from client
          ev.data.ptr = client;
          epoll_ctl(server->epollfd, EPOLL_CTL_ADD, ev.data.fd, &ev);
          CONTINUE_IF_FAIL("epoll_ctl for incoming control connection")

          server->num_client++; // a new client is added.
#undef CONTINUE_IF_FAIL
        } else {
          ftp_client_t* client = server->events[i].data.ptr;
          update_client(client);
        }
      }
  }
}