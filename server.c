#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <signal.h>
#include "utils.h"
#include "server.h"

ftp_server_t *create_ftp_server(int cntl_port, const char *basepath, const char *if_prefix) {
  ftp_server_t *server = (ftp_server_t *) malloc(sizeof(ftp_server_t));
  memset(server, 0, sizeof(ftp_server_t));

  /* initialize base path */
  if (realpath(basepath, server->basepath) == NULL) {
    perror("Unable to open server root directory");
    exit(EXIT_FAILURE);
  }

  /* initialize cntl */
  server->cntl_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server->cntl_listen_fd == -1) {
    perror("socket() of cntl_listen_fd");
    exit(EXIT_FAILURE);
  }
  memset(&server->listen_addr, 0, sizeof(server->listen_addr));
  struct sockaddr_in *first_addr = (struct sockaddr_in *) get_first_inet_addr_with_prefix(if_prefix);
  if (first_addr == NULL) {
    fprintf(stderr, "Cannot find the requested interface");
    exit(EXIT_FAILURE);
  }
  server->listen_addr.sin_family = AF_INET;
  server->listen_addr.sin_port = htons(cntl_port);
  server->listen_addr.sin_addr.s_addr = first_addr->sin_addr.s_addr;
  free(first_addr);

  if (bind(server->cntl_listen_fd, (struct sockaddr *) &server->listen_addr, sizeof(server->listen_addr)) == -1) {
    perror("bind() of cntl_listen_fd");
    exit(EXIT_FAILURE);
  }
  if (listen(server->cntl_listen_fd, MAX_CLIENT) == -1) {
    perror("listen() of cntl_listen_fd");
    exit(EXIT_FAILURE);
  }
  if (fcntl(server->cntl_listen_fd, F_SETFL, O_NONBLOCK) == -1) {
    perror("fcntl(): cannot set cntl_listen_fd to be nonblock");
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "Server listening at %s:%d.\n", inet_ntoa(server->listen_addr.sin_addr), cntl_port);

  /* initialize epoll */
  server->epollfd = epoll_create1(0);
  if (server->epollfd == -1) {
    perror("epoll_create1");
    exit(EXIT_FAILURE);
  }

  /* add cntl_sock to epoll */
  if (epoll_ctl(server->epollfd, EPOLL_CTL_ADD, server->cntl_listen_fd,
                &(struct epoll_event) {.data.ptr = NULL, .events = EPOLLIN}) == -1) {
    perror("epoll_ctl for cntl_listen_fd");
    exit(EXIT_FAILURE);
  }

  return server;
}

_Noreturn void server_loop(ftp_server_t *server) {
  signal(SIGPIPE, SIG_IGN);
  while (true) {
    server->num_closed = 0;
    server->num_event = epoll_wait(server->epollfd, server->events, MAX_EVENTS, -1);
    if (server->num_event == -1 && errno != EINTR) { // for debugger compatibility
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
        errno = 0;
        client->cntl_fd = accept(server->cntl_listen_fd,
                                 (struct sockaddr *) &(client->cntl_sockaddr),
                                 &(client->cntl_sockaddr_len));
        CONTINUE_IF_FAIL("accept")

        // STEP 2: try to set non-blocking
        errno = 0;
        fcntl(client->cntl_fd, F_SETFL, O_NONBLOCK);
        CONTINUE_IF_FAIL("fcntl")

        // STEP 3: use epoll to watch the socket
        errno = 0;
        epoll_ctl(server->epollfd, EPOLL_CTL_ADD, client->cntl_fd,
                  &(struct epoll_event) {.data.ptr = client, .events = EPOLLIN});
        CONTINUE_IF_FAIL("epoll_ctl for incoming control connection")

        server->num_client++; // a new client is added.
        update_client(client);
#undef CONTINUE_IF_FAIL
      } else {
        ftp_client_t *client = server->events[i].data.ptr;
        bool ok = true;
        for (int i = 0; i < server->num_closed; i++) {
          if (client == server->closed[i]) ok = false;
        }
        if (ok) {
          update_client(client);
        }
      }
    }
  }
}