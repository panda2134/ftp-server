#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include "command.h"
#include "global.h"
#include "utils.h"

bool read_command_buf(ftp_client_t *client) {
  ssize_t nbytes;
  int fd = client->cntl_fd;
  char *buf = client->cntl_read_buf;

  while (true) {
    if (client->cntl_bytes_read == sizeof(client->cntl_read_buf)) {
      // reset the buffer position, i.e. take 8192 bytes away from the front of the buffer.
      client->cntl_bytes_read = 0;
    }
    errno = 0;
    nbytes = read(fd, buf + client->cntl_bytes_read, BUF_SIZE - client->cntl_bytes_read);
    if (nbytes == -1) {
      if (errno != EAGAIN) {
        perror("Error reading command buffer");
      }
      return false;
    }
    ssize_t original_bytes_read = client->cntl_bytes_read;
    client->cntl_bytes_read += nbytes;

    if (client->state != S_CMD) return false; // only read control command at state S_CMD

    for (ssize_t i = original_bytes_read; i + 1 < client->cntl_bytes_read; i++) {
      if (buf[i] == '\r' && buf[i + 1] == '\n') { // found line termination
        memcpy(client->last_cmd, client->cur_cmd, sizeof(client->last_cmd));
        memcpy(client->last_argument, client->argument, sizeof(client->argument));
        client->last_verb = client->verb;
        memcpy(client->cur_cmd, buf, i * sizeof(char));
        client->cur_cmd[i] = '\0';

        ssize_t offset = i + 2;
        for (ssize_t j = 0; j + offset < client->cntl_bytes_read; j++)
          buf[j] = buf[j + offset];
        client->cntl_bytes_read -= offset;

        return true;
      }
    }
  }
}

void parse_command(ftp_client_t *client) {
  // first, search for the space
  const char *command = client->cur_cmd;
  int verb_length;
  for (verb_length = 0; verb_length < BUF_SIZE; verb_length++) {
    if (command[verb_length] == '\0' || command[verb_length] == ' ') {
      break;
    }
  }
  if (verb_length >= BUF_SIZE) {
    client->verb = UNKNOWN_VERB; /* too long, invalid verb */
    client->argument[0] = '\0';
    return;
  } else {
    char *current_verb = strndup(command, BUF_SIZE);
    current_verb[verb_length] = '\0';

    /* 1. parse argument */
    if (command[verb_length] == '\0') {
      /* verb with no arguments */
      client->argument[0] = '\0';
    } else {
      /* command[verb_length] is a space, and an argument exists */
      strncpy(client->argument, current_verb + verb_length + 1, BUF_SIZE);
    }
    /* 2. parse verb */
    for (int i = 0; i < UNKNOWN_VERB; i++) {
      if (strcmp(current_verb, VERB_STR[i]) == 0) {
        client->verb = i;
        goto end;
      }
    }
    client->verb = UNKNOWN_VERB;
    end:
    free(current_verb);
  }
}

void execute_command(ftp_client_t *client) {
  if (client->state == S_RESPONSE_END) {
    if (client->cntl_write_buf[0] == '4' || client->cntl_write_buf[0] == '5') {
      client->last_failed = true;
    }
    errno = 0;
    epoll_ctl(client->server->epollfd, EPOLL_CTL_MOD, client->cntl_fd,
              &(struct epoll_event) {.data.ptr = client, .events = EPOLLIN});
    if (errno) {
      fprintf(stderr, "Error switching control connection to EPOLLIN: %s\n", strerror(errno));
    }
    client->state = S_CMD;
  } else {
    /* simply call the verb handler here */
    assert(client->verb >= 0 && client->verb < NUM_REQUEST_VERB);
    VERB_HANDLER[client->verb](client);
  }
}

void prepare_cntl_message_write_alt(ftp_client_t *client, const char *str, size_t len, client_state response_state) {
  memcpy(client->cntl_write_buf, str, len);
  strcpy(client->cntl_write_buf + len, "\r\n");
  client->cntl_bytes_written = 0u;
  client->cntl_bytes_to_write = strlen(client->cntl_write_buf);

  errno = 0;
  epoll_ctl(client->server->epollfd,
            EPOLL_CTL_MOD,
            client->cntl_fd,
            &(struct epoll_event) {.data.ptr = client, .events = EPOLLOUT}
  );
  if (errno) {
    fprintf(stderr, "Error switching control connection to EPOLLOUT: %s\n", strerror(errno));
    return;
  }
  client->state = response_state;
}

void prepare_cntl_message_write(ftp_client_t *client, const char *str, client_state response_state) {
  prepare_cntl_message_write_alt(client, str, strlen(str), response_state);
}

bool write_cntl_message(ftp_client_t *client, client_state work_state) {
  while (client->cntl_bytes_written < client->cntl_bytes_to_write) {
    ssize_t delta = write(client->cntl_fd, client->cntl_write_buf,
                                        client->cntl_bytes_to_write - client->cntl_bytes_written);
    if (delta == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return false;
      } else {
        fprintf(stderr, "Error writing response: %s\n", strerror(errno));
        return false;
      }
    } else {
      client->cntl_bytes_written += delta;
    }
  }
  if (client->cntl_bytes_written >= client->cntl_bytes_to_write) {
    epoll_ctl(client->server->epollfd,
              EPOLL_CTL_MOD,
              client->cntl_fd,
              &(struct epoll_event) {.data.ptr = client, .events = EPOLLIN});
    client->state = work_state;
    execute_command(client); // drop into a work state, where epoll_ctl is then called
    return true;
  }
}

#define DEFAULT_MEANS_INVALID_STATE(s) default: fprintf(stderr, "Invalid state for"s); break;

void simple_response_handler(ftp_client_t *client, const char *response, client_state next_state) {
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      prepare_cntl_message_write(client, response, S_RESPONSE_0);
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, next_state);
      break;
    DEFAULT_MEANS_INVALID_STATE("USER")
  }
}

void quit_handler(ftp_client_t *client) {
  simple_response_handler(client, "221 Bye", S_QUIT);
}

void syst_handler(ftp_client_t *client) {
  simple_response_handler(client, "215 UNIX Type: L8", S_RESPONSE_END);
}

void unknown_handler(ftp_client_t *client) {
  simple_response_handler(client, "500 Syntax error", S_RESPONSE_END);
}

void type_handler(ftp_client_t *client) {
  if (strcmp(client->argument, "I") == 0) {
    simple_response_handler(client, "200 Type set to I", S_RESPONSE_END);
  } else {
    simple_response_handler(client, "504 Unsupported type", S_RESPONSE_END);
  }
}

void user_handler(ftp_client_t *client) {
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      if (strcmp(client->argument, "anonymous") == 0) {
        prepare_cntl_message_write(client, "331 Guest login ok, send password", S_RESPONSE_0);
      } else {
        prepare_cntl_message_write(client, "530 Invalid credential", S_RESPONSE_0);
      }
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_RESPONSE_END);
      break;
    DEFAULT_MEANS_INVALID_STATE("USER")
  }
}

void pass_handler(ftp_client_t *client) {
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      if (memcmp(client->last_cmd, "USER ", 5u) != 0 || client->last_failed) {
        prepare_cntl_message_write(client, "530 Invalid credential", S_RESPONSE_0);
      } else {
        prepare_cntl_message_write(client, "230 Guest login ok, access restrictions apply", S_RESPONSE_0);
      }
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_RESPONSE_END);
      break;
    DEFAULT_MEANS_INVALID_STATE("PASS")
  }
}

void fs_generic_handler_with_argument(ftp_client_t *client, int fn(const char *)) {
  char *resolved_path;
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      resolved_path = malloc(PATH_MAX);

      errno = 0;
      realpath(client->argument, resolved_path);
      if (errno) {
        char *message = malloc(BUF_SIZE);
        sprintf(message, "550 %s", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_0);
        free(message);
        goto case_cleanup_cwd;
      }
      if (!is_valid_path(client->server, resolved_path)) {
        prepare_cntl_message_write(client, "550 Path not allowed", S_RESPONSE_0);
        goto case_cleanup_cwd;
      }
      errno = 0;
      fn(resolved_path);
      if (errno) {
        char *message = malloc(BUF_SIZE);
        sprintf(message, "550 %s", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_0);
        free(message);
        goto case_cleanup_cwd;
      }
      prepare_cntl_message_write(client, "250 Okay", S_RESPONSE_0);

    case_cleanup_cwd:
      free(resolved_path);
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_RESPONSE_END);
    DEFAULT_MEANS_INVALID_STATE("CWD/MKD/RMD/DELE")
  }
}

int mkdir_alt(const char *pathname) {
  return mkdir(pathname, 0);
}

void mkd_handler(ftp_client_t *client) {
  fs_generic_handler_with_argument(client, mkdir_alt);
}

void cwd_handler(ftp_client_t *client) {
  fs_generic_handler_with_argument(client, chdir);
}

void rmd_handler(ftp_client_t *client) {
  fs_generic_handler_with_argument(client, rmdir);
}

void dele_handler(ftp_client_t *client) {
  fs_generic_handler_with_argument(client, remove);
}

void pwd_handler(ftp_client_t *client) {
  char message[BUF_SIZE];
  size_t encoded_len = 0u;
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      strcpy(message, "250 ");
      encode_pwd(client->cwd, message + 4, &encoded_len); // "4" is for "250 "
      prepare_cntl_message_write_alt(client, message, encoded_len + 4, S_RESPONSE_0);
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_RESPONSE_END);
    DEFAULT_MEANS_INVALID_STATE("PWD")
  }
}

void rnfr_handler(ftp_client_t *client) {
  struct stat stat_info;
  char resolved_path[PATH_MAX];
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      errno = 0;
      realpath(client->argument, resolved_path);
      if (errno) {
        char *message = malloc(BUF_SIZE);
        sprintf(message, "550 %s", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_0);
        free(message);
        goto case_cleanup_rnfr;
      }
      if (!is_valid_path(client->server, resolved_path)) {
        prepare_cntl_message_write(client, "550 Path not allowed", S_RESPONSE_0);
        goto case_cleanup_rnfr;
      }
      errno = 0;
      stat(client->argument, &stat_info);
      if (errno) {
        char *message = malloc(BUF_SIZE);
        sprintf(message, "550 %s", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_0);
        free(message);
        goto case_cleanup_rnfr;
      }
      prepare_cntl_message_write(client, "350 File found", S_RESPONSE_0);
    case_cleanup_rnfr:
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_RESPONSE_END);
      break;
    DEFAULT_MEANS_INVALID_STATE("RNFR")
  }
}

void rnto_handler(ftp_client_t *client) {
  struct stat stat_info;
  char resolved_path_new[PATH_MAX], resolved_path_old[PATH_MAX];
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      if (strcmp(client->last_cmd, "RNFR") != 0 || client->last_failed) {
        prepare_cntl_message_write(client, "503 Bad sequence of commands", S_RESPONSE_0);
        break;
      }

      realpath(client->last_argument, resolved_path_old); // this should not cause any error, as it's already checked
      errno = 0;
      realpath(client->argument, resolved_path_new);
      if (errno) {
        char *message = malloc(BUF_SIZE);
        sprintf(message, "550 %s", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_0);
        free(message);
        goto case_cleanup_rnto;
      }
      if (!is_valid_path(client->server, resolved_path_new)) {
        prepare_cntl_message_write(client, "550 Path not allowed", S_RESPONSE_0);
        goto case_cleanup_rnto;
      }
      errno = 0;
      stat(resolved_path_new, &stat_info);
      if (errno) {
        char *message = malloc(BUF_SIZE);
        sprintf(message, "550 %s", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_0);
        free(message);
        goto case_cleanup_rnto;
      }
      errno = 0;
      rename(resolved_path_old, resolved_path_new);
      if (errno) {
        char *message = malloc(BUF_SIZE);
        sprintf(message, "550 %s", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_0);
        free(message);
        goto case_cleanup_rnto;
      }
      prepare_cntl_message_write(client, "250 File renamed", S_RESPONSE_0);

    case_cleanup_rnto:
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_RESPONSE_END);
      break;
    DEFAULT_MEANS_INVALID_STATE("RNFR")
  }
}

void shutdown_all_data_connection(ftp_client_t *client) {
  if (client->data_fd != -1) {
    if (shutdown(client->data_fd, SHUT_WR)) { perror("closing client->data_fd"); }
    client->data_fd = -1;
  }
  if (client->mode == M_PASV && client->pasv_listen_fd != -1) {
    if (shutdown(client->pasv_listen_fd, SHUT_WR)) { perror("closing client->pasv_listen_fd"); }
    client->pasv_listen_fd = -1;
  }
}

void port_handler(ftp_client_t *client) {
  unsigned h1, h2, h3, h4, p1, p2;
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      if (sscanf(client->argument, "%u,%u,%u,%u,%u,%u", // NOLINT(cert-err34-c)
                 &h1, &h2, &h3, &h4, &p1, &p2) == 6) {
        shutdown_all_data_connection(client);

        client->mode = M_PORT;
        errno = 0;
        client->data_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (errno) {
          perror("creating socket in PORT");
          break;
        }
        errno = 0;
        fcntl(client->data_fd, F_SETFL, O_NONBLOCK);
        if (errno) {
          perror("fcntl in PORT");
          break;
        }

        client->data_sockaddr.sin_family = AF_INET;
        client->data_sockaddr.sin_addr.s_addr =
            htonl((h1 << 24) | (h2 << 16) | (h3 << 8) | h4);
        client->data_sockaddr.sin_port = (uint16_t) ((p1 << 8) | p2);

        prepare_cntl_message_write(client, "200 PORT command successful", S_RESPONSE_0);

      } else {
        prepare_cntl_message_write(client, "550 Malformed address format", S_RESPONSE_0);
      }

      break;
    case S_RESPONSE_0:
      break;
    DEFAULT_MEANS_INVALID_STATE("PORT")
  }
}

void pasv_handler(ftp_client_t *client) {
  unsigned h1, h2, h3, h4, p1, p2;
  char message[BUF_SIZE];
  bool fail = false;
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      shutdown_all_data_connection(client);
      client->mode = M_PASV;
      errno = 0;
      client->pasv_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
      if (errno) {
        perror("creating socket in PASV");
        fail = true;
        goto case_cleanup_pasv;
      }

      errno = 0;
      listen(client->pasv_listen_fd, 1);
      if (errno) {
        perror("listen() in PASV");
        fail = true;
        goto case_cleanup_pasv;
      }

      errno = 0;
      getsockname(client->pasv_listen_fd,
                  (struct sockaddr*)&client->pasv_listen_sockaddr,
                  &client->pasv_listen_sockaddr_len);
      h1 = ntohl(client->pasv_listen_sockaddr.sin_addr.s_addr) >> 24;
      h2 = (ntohl(client->pasv_listen_sockaddr.sin_addr.s_addr) >> 16) & 0xff;
      h3 = (ntohl(client->pasv_listen_sockaddr.sin_addr.s_addr) >> 8) & 0xff;
      h4 = ntohl(client->pasv_listen_sockaddr.sin_addr.s_addr) & 0xff;
      p1 = (client->pasv_listen_sockaddr.sin_port >> 8) & 0xff;
      p2 = (client->pasv_listen_sockaddr.sin_port) & 0xff;
      if (errno) {
        perror("getsockname() in PASV");
        fail = true;
        goto case_cleanup_pasv;
      }

      errno = 0;
      fcntl(client->pasv_listen_fd, F_SETFL, O_NONBLOCK);
      if (errno) {
        perror("fcntl() in PASV");
        fail = true;
        goto case_cleanup_pasv;
      }

      if (epoll_ctl(client->server->epollfd, EPOLL_CTL_ADD, client->pasv_listen_fd,
                    &(struct epoll_event) {.data.ptr = client, .events = EPOLLIN}) == -1) {
        perror("epoll_ctl() in PASV");
        fail = true;
        goto case_cleanup_pasv;
      }

    case_cleanup_pasv:
      if (!fail) {
        sprintf(message, "200 Entering Passive Mode (%u, %u, %u, %u, %u, %u)",
                h1, h2, h3, h4, p1, p2);
      } else {
        strcpy(message, "550 Failed to create socket");
      }
      prepare_cntl_message_write(client, message, S_RESPONSE_0);
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_RESPONSE_END);
      break;
    DEFAULT_MEANS_INVALID_STATE("PASV")
  }
}

void list_handler(ftp_client_t *client) {
  struct stat stat_info;
  char message[BUF_SIZE];
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      if (stat(client->cwd, &stat_info) == -1) {
        sprintf(message, "550 Cannot list current directory; %s", strerror(errno));
      } else {
        strcpy(message, "150 Listing directory");
      }
      client->cur_dir_ptr = opendir(client->cwd);
      prepare_cntl_message_write(client, message, S_RESPONSE_0);
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_WORK_DATA);
      break;
    case S_WORK_DATA:
      errno = 0;
      client->cur_dir_ent = readdir(client->cur_dir_ptr);
      if (errno) {
        perror("LIST at S_DATA_BUF");
        epoll_ctl(client->server->epollfd, EPOLL_CTL_DEL, client->data_fd, NULL);
        close(client->data_fd);
        prepare_cntl_message_write(client, "451 Failed to list current directory",
                                   S_RESPONSE_1);
        break;
      } else {
        if (client->cur_dir_ent == NULL) {
          epoll_ctl(client->server->epollfd, EPOLL_CTL_DEL, client->data_fd, NULL);
          close(client->data_fd);
          prepare_cntl_message_write(client, "226 Successfully listed current directory",
                                     S_RESPONSE_1);
          break;
        } else {
          strcpy(client->data_write_buf, client->cur_dir_ent->d_name);
          strcat(client->data_write_buf, " ");
          client->data_bytes_written = 0u;
          client->data_bytes_to_write = strlen(client->data_write_buf);

          client->state = S_DATA_BUF;
          epoll_ctl(client->server->epollfd, EPOLL_CTL_MOD, client->data_fd,
                    &(struct epoll_event){ .data.ptr = client, .events = EPOLLOUT });
        }
      }
      break;
    case S_DATA_BUF:
      while (client->data_bytes_written < client->data_bytes_to_write) {
        errno = 0;
        client->data_bytes_written += write(client->data_fd, client->data_write_buf,
                                            client->data_bytes_to_write - client->data_bytes_written);
        if (errno && !(errno == EAGAIN || errno == EWOULDBLOCK)) {
          fprintf(stderr, "Error writing response: %s\n", strerror(errno));
          epoll_ctl(client->server->epollfd, EPOLL_CTL_DEL, client->data_fd, NULL);
          close(client->data_fd);
          prepare_cntl_message_write(client, "451 Failed to list current directory",
                                     S_RESPONSE_1);
        }
      }
      if (client->data_bytes_written >= client->data_bytes_to_write) {
        client->state = S_WORK_DATA;
        execute_command(client);
      }
      break;
    DEFAULT_MEANS_INVALID_STATE("LIST")
  }
}

void retr_stor_handler(ftp_client_t *client) {
  struct stat stat_info;
  char resolved_path[PATH_MAX], message[BUF_SIZE], read_buf[BUF_SIZE];
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      if (realpath(client->argument, resolved_path) == NULL) {
        sprintf(message, "550 %s", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_1);
      } else if (!is_valid_path(client->server, resolved_path)) {
        prepare_cntl_message_write(client, "550 Path not allowed", S_RESPONSE_1);
      } else {
        if (client->verb == RETR) {
          if (stat(resolved_path, &stat_info) == -1) {
            sprintf(message, "550 %s", strerror(errno));
            prepare_cntl_message_write(client, message, S_RESPONSE_1);
          } else {
            sprintf(message, "150 Opening BINARY mode data connection for %s (%ld bytes)",
                    resolved_path, stat_info.st_size);
            prepare_cntl_message_write(client, message, S_RESPONSE_0);
          }
        } else {
          sprintf(message, "150 Opening BINARY mode data connection for %s",
                  resolved_path);
          prepare_cntl_message_write(client, message, S_RESPONSE_0);
        }
      }
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_WORK_DATA);
      break;
    case S_WORK_DATA:
      client->local_fd = open(client->argument,
                              client->verb == RETR ? O_RDONLY : O_CREAT | O_WRONLY);
      if (client->local_fd == -1) {
        sprintf(message, "551 %s", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_1);
        goto case_cleanup_retr_work_data;
      }
      if (client->mode == M_PORT) {
        if (connect(client->data_fd, (struct sockaddr*)&client->data_sockaddr, client->data_sockaddr_len) == -1 &&
          errno != EINPROGRESS && errno != EALREADY) { // error!

          sprintf(message, "551 %s", strerror(errno));
          shutdown_all_data_connection(client);
          prepare_cntl_message_write(client, message, S_RESPONSE_1);
          goto case_cleanup_retr_work_data;
        } else {
          errno = 0;
          epoll_ctl(client->server->epollfd, EPOLL_CTL_ADD, client->data_fd,
                    &(struct epoll_event){
                            .data.ptr = client, .events = client->verb == RETR ? EPOLLOUT : EPOLLIN
          });
          if (errno) {
            fprintf(stderr, "epoll_ctl in RETR/STOR: %s\n", strerror(errno));
          }
        }
      } else { // PASV, should be already added
        if (client->data_fd == -1) {
          prepare_cntl_message_write(client, "425 No data connection", S_RESPONSE_1);
          goto case_cleanup_retr_work_data;
        }
        errno = 0;
        epoll_ctl(client->server->epollfd, EPOLL_CTL_MOD, client->data_fd,
                  &(struct epoll_event){
                          .data.ptr = client,
                          .events = client->verb == RETR ? EPOLLOUT : EPOLLIN
        });
        if (errno) {
          fprintf(stderr, "epoll_ctl in RETR: %s\n", strerror(errno));
        }
      }
      if (client->verb == RETR) {
        client->data_bytes_written = 0;
        client->data_bytes_to_write = lseek(client->local_fd, 0, SEEK_END);
        lseek(client->local_fd, 0, SEEK_SET);
        client->state = S_DATA_SENDFILE;
      } else {
        client->state = S_DATA_BUF;
      }
    case_cleanup_retr_work_data:
      break;
    case S_DATA_SENDFILE:
      while (client->data_bytes_written < client->data_bytes_to_write) {
        ssize_t delta = sendfile(client->data_fd, client->local_fd, NULL,
                             client->data_bytes_to_write - client->data_bytes_written);
        if (delta == -1) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            goto case_cleanup_retr_stor_data_sendfile; // wait for next epoll
          } else {
            sprintf(message, "426 Data connection failure: %s", strerror(errno));
            shutdown_all_data_connection(client);
            prepare_cntl_message_write(client, message, S_RESPONSE_1);
            goto case_cleanup_retr_stor_data_sendfile;
          }
        } else {
          client->data_bytes_written += delta;
        }
      }
      if (client->data_bytes_written >= client->data_bytes_to_write) {
        shutdown_all_data_connection(client);
        prepare_cntl_message_write(client, "226 Transfer complete", S_RESPONSE_1);
      }
    case_cleanup_retr_stor_data_sendfile:
      break;
    case S_DATA_BUF:
      while (true) {
        ssize_t read_len = read(client->data_fd, read_buf, BUF_SIZE);
        if (read_len == -1) {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // broken pipe
            sprintf(message, "426 Data connection failure: %s", strerror(errno));
            prepare_cntl_message_write(client, message, S_RESPONSE_1);
          }
          goto case_cleanup_retr_stor_data_buf;
        } else if (read_len == 0) {
          // closed
          shutdown_all_data_connection(client);
          prepare_cntl_message_write(client, "226 Transfer complete", S_RESPONSE_1);
          goto case_cleanup_retr_stor_data_buf;
        } else {
          if (write(client->local_fd, read_buf, read_len) == -1) {
            sprintf(message, "552 %s", strerror(errno));
            shutdown_all_data_connection(client);
            prepare_cntl_message_write(client, message, S_RESPONSE_1);
            goto case_cleanup_retr_stor_data_buf;
          }
        }
      }
    case_cleanup_retr_stor_data_buf:
      break;
    case S_RESPONSE_1:
      write_cntl_message(client, S_RESPONSE_END);
    DEFAULT_MEANS_INVALID_STATE("RETR")
  }
}

#undef DEFAULT_MEANS_INVALID_STATE