#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "client.h"
#include "command.h"

void create_client(ftp_client_t* client, ftp_server_t* server) {
  assert(client != NULL);
  memset(client, 0, sizeof(ftp_client_t));
  client->cntl_fd = client->data_fd
      = client->pasv_listen_fd = client->local_fd = -1;
  client->state = S_CMD;
  client->mode = M_UNKNOWN;
  client->server = server;
  strcpy(client->cwd, server->basepath);
}

void try_accept_pasv(ftp_client_t* client) {
  if (client->mode != M_PASV || client->data_fd != -1) {
    return;
  }
  client->data_fd = accept(client->pasv_listen_fd,
                           (struct sockaddr*)&client->data_sockaddr,
                               &client->data_sockaddr_len);
  if (errno) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      perror("accept() with pasv socket");
      client->data_fd = -1; /* ensure that data_fd is -1 */
    } else {
      /* not an error; still waiting for a connection */
    }
  } else {
    epoll_ctl(client->server->epollfd, EPOLL_CTL_ADD, client->data_fd,
              &(struct epoll_event){.data.ptr = client, .events = EPOLLIN});
    if (errno) {
      perror("epoll_ctl() when trying to accept a PASV connection");
      close(client->data_fd);
      client->data_fd = -1;
    }
  }
}

void update_client(ftp_client_t* client) {
  assert(client != NULL);

  chdir(client->cwd);
  try_accept_pasv(client);

  switch (client->state) {
    case S_CMD:
      if (read_command_buf(client)) {
        parse_command(client);
        client->state = S_WORK_RESPONSE_0;
        execute_command(client);
      }
      break;
    default:
      execute_command(client);
      break;
  }
  chdir(client->server->basepath);
  if (client->state == S_QUIT) {
    ftp_server_t *server = client->server;
    if (client->cntl_fd >= 0) {
      epoll_ctl(server->epollfd, EPOLL_CTL_DEL, client->cntl_fd, NULL);
      close(client->cntl_fd);
    }
    if (client->data_fd >= 0) {
      epoll_ctl(server->epollfd, EPOLL_CTL_DEL, client->data_fd, NULL);
      close(client->data_fd);
    }
    free(client);
    server->num_client--;
  }
}