#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include "command.h"
#include "global.h"
#include "utils.h"


void (*VERB_HANDLER[NUM_REQUEST_VERB])(ftp_client_t *) = {
    [QUIT] = quit_handler, [ABOR] = quit_handler, [SYST] = syst_handler,
    [PASV] = pasv_handler, [PWD] = pwd_handler, [USER] = user_handler, [PASS] = pass_handler,
    [RETR] = retr_stor_handler, [STOR] = retr_stor_handler, [TYPE] = type_handler, [PORT] = port_handler,
    [MKD] = mkd_handler, [CWD] = cwd_handler, [LIST] = list_handler, [NLST] = list_handler, [RMD] = rmd_handler,
    [RNFR] = rnfr_handler, [RNTO] = rnto_handler, [DELE] = dele_handler, [CDUP] = cdup_handler,
    [UNKNOWN_VERB] = unknown_handler, [INIT] = init_handler
};

const char *VERB_STR[NUM_REQUEST_VERB] = {
    [QUIT] = "QUIT", [ABOR] = "ABOR", [SYST] = "SYST", [PASV] = "PASV", [PWD] = "PWD",
    [USER] = "USER", [PASS] = "PASS", [RETR] = "RETR", [STOR] = "STOR", [TYPE] = "TYPE",
    [PORT] = "PORT", [MKD] = "MKD", [CWD] = "CWD", [LIST] = "LIST", [NLST] = "NLST", [RMD] = "RMD",
    [RNFR] = "RNFR", [RNTO] = "RNTO", [DELE] = "DELE", [CDUP] = "CDUP", [UNKNOWN_VERB] = "", [INIT] = ""
};


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
    } else if (nbytes == 0) {
      client->state = S_QUIT;
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
  if (verb_length >= BUF_SIZE || verb_length == 0) {
    client->verb = UNKNOWN_VERB; /* too long or too short, invalid verb */
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
    client->last_failed = client->cntl_write_buf[0] == '4' || client->cntl_write_buf[0] == '5';
    errno = 0;
    epoll_ctl(client->server->epollfd, EPOLL_CTL_MOD, client->cntl_fd,
              &(struct epoll_event) {.data.ptr = client, .events = EPOLLIN});
    if (errno) {
      fprintf(stderr, "Error switching control connection to EPOLLIN: %s\n", strerror(errno));
    }
    client->state = S_CMD;
  } else if (client->state == S_QUIT) {
    return; // do nothing
  } else {
    /* simply call the verb handler here */
    assert(client->verb >= 0 && client->verb < NUM_REQUEST_VERB);
    VERB_HANDLER[client->verb](client);
  }
}

void prepare_cntl_message_write_alt(ftp_client_t *client, const char *str, size_t len, int response_state) {
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

void prepare_cntl_message_write(ftp_client_t *client, const char *str, int response_state) {
  prepare_cntl_message_write_alt(client, str, strlen(str), response_state);
}

bool write_cntl_message(ftp_client_t *client, int work_state) {
  while (client->cntl_bytes_written < client->cntl_bytes_to_write) {
    ssize_t delta = write(client->cntl_fd, client->cntl_write_buf,
                                        client->cntl_bytes_to_write - client->cntl_bytes_written);
    if (delta == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return false;
      } else {
        fprintf(stderr, "Error writing response: %s\n", strerror(errno));
        client->state = S_QUIT;
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
  } else {
    return false;
  }
}

#define DEFAULT_MEANS_INVALID_STATE(s) default: fprintf(stderr, "Invalid state for "s".\n"); break;

void simple_response_handler(ftp_client_t *client, const char *response, int next_state) {
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      prepare_cntl_message_write(client, response, S_RESPONSE_0);
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, next_state);
      break;
    DEFAULT_MEANS_INVALID_STATE("simple_response_handler")
  }
}

void quit_handler(ftp_client_t *client) {
  simple_response_handler(client, "221 Bye.", S_QUIT);
}

void syst_handler(ftp_client_t *client) {
  simple_response_handler(client, "215 UNIX Type: L8", S_RESPONSE_END);
}

void unknown_handler(ftp_client_t *client) {
  simple_response_handler(client, "500 Syntax error.", S_RESPONSE_END);
}

void init_handler(ftp_client_t *client) {
  simple_response_handler(client, "220 localhost FTP server ready.", S_RESPONSE_END);
}

void type_handler(ftp_client_t *client) {
  /* treat all TYPE as setting to BINARY */
  simple_response_handler(client, "200 Type set to I.", S_RESPONSE_END);
}

void user_handler(ftp_client_t *client) {
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      if (strcmp(client->argument, "anonymous") == 0) {
        prepare_cntl_message_write(client, "331 Guest login ok, send password.", S_RESPONSE_0);
      } else {
        prepare_cntl_message_write(client, "530 Invalid credential.", S_RESPONSE_0);
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
      if (client->last_verb != USER || client->last_failed) {
        prepare_cntl_message_write(client, "530 Invalid credential.", S_RESPONSE_0);
      } else {
        prepare_cntl_message_write(client, "230 Guest login ok, access restrictions apply.", S_RESPONSE_0);
      }
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_RESPONSE_END);
      break;
    DEFAULT_MEANS_INVALID_STATE("PASS")
  }
}

void fs_generic_handler_with_argument(ftp_client_t *client, int fn(const char *)) {
  char path_buf[PATH_MAX];
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      realpath(client->cwd, path_buf);
      strcat(path_buf, "/");
      strcat(path_buf, client->argument);
      if (!is_valid_path(client->server, path_buf)) {
        prepare_cntl_message_write(client, "550 Path not allowed or malformed.", S_RESPONSE_0);
        goto case_cleanup_cwd;
      }
      errno = 0;
      fn(path_buf);
      if (errno) {
        char *message = malloc(BUF_SIZE);
        sprintf(message, "550 %s.", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_0);
        free(message);
        goto case_cleanup_cwd;
      }
      prepare_cntl_message_write(client, "250 Okay.", S_RESPONSE_0);

    case_cleanup_cwd:
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_RESPONSE_END);
      break;
    DEFAULT_MEANS_INVALID_STATE("CWD/MKD/RMD/DELE")
  }
}

int mkdir_alt(const char *pathname) {
  return mkdir(pathname, 0777);
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

void cdup_handler(ftp_client_t *client) {
  char path_buf[PATH_MAX];
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      realpath(client->cwd, path_buf);
      strcat(path_buf, "/..");
      if (!is_valid_path(client->server, path_buf)) {
        prepare_cntl_message_write(client, "550 Path not allowed or malformed.", S_RESPONSE_0);
        goto case_cleanup_cwd;
      }
      errno = 0;
      chdir(path_buf);
      if (errno) {
        char *message = malloc(BUF_SIZE);
        sprintf(message, "550 %s.", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_0);
        free(message);
        goto case_cleanup_cwd;
      }
      prepare_cntl_message_write(client, "250 Okay", S_RESPONSE_0);

    case_cleanup_cwd:
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_RESPONSE_END);
      break;
    DEFAULT_MEANS_INVALID_STATE("CDUP")
  }
}

int delete_alt(const char *pathname) {
  /* ensure that pathname doesn't name a directory before use. */
  struct stat stat_info;
  if (stat(pathname, &stat_info) == -1) {
    return -1;
  }
  if (S_ISDIR(stat_info.st_mode)) {
    errno = EISDIR;
    return -1;
  }
  return unlink(pathname);
}

void dele_handler(ftp_client_t *client) {
  fs_generic_handler_with_argument(client, unlink);
}

void pwd_handler(ftp_client_t *client) {
  char buf[BUF_SIZE], message[BUF_SIZE];
  size_t encoded_len = 0u;
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      strcpy(message, "257 ");
      if (strcmp(client->server->basepath, client->cwd) != 0) {
        realpath(client->cwd, buf);
        encode_pwd(buf + strlen(client->server->basepath), message + 4, &encoded_len); // "4" is for "257 "
      } else {
        encode_pwd("/", message + 4, &encoded_len); // "4" is for "250 "
      }
      prepare_cntl_message_write_alt(client, message, encoded_len + 4, S_RESPONSE_0);
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_RESPONSE_END);
      break;
    DEFAULT_MEANS_INVALID_STATE("PWD")
  }
}

void rnfr_handler(ftp_client_t *client) {
  struct stat stat_info;
  char resolved_path[PATH_MAX];
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      strcpy(resolved_path, client->cwd);
      strcat(resolved_path, "/");
      strcat(resolved_path, client->argument);
      if (!is_valid_path(client->server, resolved_path)) {
        prepare_cntl_message_write(client, "550 Path not allowed or malformed.", S_RESPONSE_0);
        goto case_cleanup_rnfr;
      }
      errno = 0;
      stat(client->argument, &stat_info);
      if (errno) {
        char *message = malloc(BUF_SIZE);
        sprintf(message, "550 %s.", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_0);
        free(message);
        goto case_cleanup_rnfr;
      }
      prepare_cntl_message_write(client, "350 File found.", S_RESPONSE_0);
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
  char path_buf_new[PATH_MAX], path_buf_old[PATH_MAX];
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      if (client->last_verb != RNFR || client->last_failed) {
        prepare_cntl_message_write(client, "503 Bad sequence of commands.", S_RESPONSE_0);
        break;
      }

      realpath(client->cwd, path_buf_old);
      strcat(path_buf_old, "/");
      realpath(client->last_argument, path_buf_old);

      realpath(client->cwd, path_buf_new);
      strcat(path_buf_new, "/");
      strcat(path_buf_new, client->argument);
      if (!is_valid_path(client->server, path_buf_new)) {
        prepare_cntl_message_write(client, "550 Path not allowed or malformed.", S_RESPONSE_0);
        goto case_cleanup_rnto;
      }
      errno = 0;
      stat(path_buf_new, &stat_info);
      if (errno != ENOENT) {
        char *message = malloc(BUF_SIZE);
        if (errno) {
          sprintf(message, "550 %s.", strerror(errno));
          prepare_cntl_message_write(client, message, S_RESPONSE_0);
        } else {
          prepare_cntl_message_write(client, "550 File exists.", S_RESPONSE_0);
        }
        free(message);
        goto case_cleanup_rnto;
      }
      errno = 0;
      rename(path_buf_old, path_buf_new);
      if (errno) {
        char *message = malloc(BUF_SIZE);
        sprintf(message, "550 %s.", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_0);
        free(message);
        goto case_cleanup_rnto;
      }
      prepare_cntl_message_write(client, "250 File renamed.", S_RESPONSE_0);

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
    if (shutdown(client->data_fd, SHUT_RDWR) && errno != ENOTCONN) {
      perror("shutdown(client->data_fd)");
    }
    if (close(client->data_fd)) { perror("close(client->data_fd)"); }
    client->data_fd = -1;
  }
  if (client->mode == M_PASV && client->pasv_listen_fd != -1) {
    if (shutdown(client->pasv_listen_fd, SHUT_RDWR) && errno != ENOTCONN) {
      perror("shutdown(client->pasv_listen_fd)");
    }
    if (close(client->pasv_listen_fd)) { perror("close(client->pasv_listen_fd)"); }
    client->pasv_listen_fd = -1;
  }
  client->mode = M_UNKNOWN;
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
        client->data_sockaddr.sin_port = htons((uint16_t) ((p1 << 8) | p2));
        client->data_sockaddr_len = sizeof(client->data_sockaddr);

        prepare_cntl_message_write(client, "200 PORT command successful.", S_RESPONSE_0);

      } else {
        prepare_cntl_message_write(client, "550 Malformed address format.", S_RESPONSE_0);
      }

      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_RESPONSE_END);
      break;
    DEFAULT_MEANS_INVALID_STATE("PORT")
  }
}

void pasv_handler(ftp_client_t *client) {
  unsigned h1, h2, h3, h4, p1, p2;
  char message[BUF_SIZE];
  struct sockaddr_in addr;
  socklen_t addr_len = 0;
  memset(&addr, 0, sizeof(addr));
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

      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = client->server->listen_addr.sin_addr.s_addr;
      addr.sin_port = htons(0);
      if (bind(client->pasv_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind() in PASV");
        fail = true;
        goto case_cleanup_pasv;
      }

      if (listen(client->pasv_listen_fd, 1) == -1) {
        perror("listen() in PASV");
        fail = true;
        goto case_cleanup_pasv;
      }

      addr_len = sizeof(addr);
      if (getsockname(client->pasv_listen_fd, (struct sockaddr*)&addr, &addr_len) == -1) {
        perror("getsockname() in PASV");
        fail = true;
        goto case_cleanup_pasv;
      }
      p1 = (ntohs(addr.sin_port) >> 8) & 0xff;
      p2 = ntohs(addr.sin_port) & 0xff;

      h1 = ntohl(client->server->listen_addr.sin_addr.s_addr) >> 24;
      h2 = (ntohl(client->server->listen_addr.sin_addr.s_addr) >> 16) & 0xff;
      h3 = (ntohl(client->server->listen_addr.sin_addr.s_addr) >> 8) & 0xff;
      h4 = ntohl(client->server->listen_addr.sin_addr.s_addr) & 0xff;

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
        sprintf(message, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).", h1, h2, h3, h4, p1, p2);
      } else {
        strcpy(message, "550 Failed to create socket.");
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
  char target[BUF_SIZE], current_filename[FILENAME_SIZE], message[BUF_SIZE];
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      strcpy(target, client->cwd);
      strcat(target, "/");
      strcat(target, client->argument);
      if (!is_valid_path(client->server, target)) {
        prepare_cntl_message_write(client, "550 Path not allowed or malformed.", S_RESPONSE_1);
      } else if (stat(target, &stat_info) == -1) {
        sprintf(message, "550 %s.", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_1);
      } else if (client->mode == M_UNKNOWN) {
        prepare_cntl_message_write(client, "425 No data connection.", S_RESPONSE_1);
      } else {
        strcpy(message, "150 Making a list.");
        if ((client->cur_dir_ptr = opendir(target)) == NULL && errno != ENOTDIR) { // NULL means Error or Is a file
          sprintf(message, "550 %s.", strerror(errno));
          prepare_cntl_message_write(client, message, S_RESPONSE_1);
        } else {
          prepare_cntl_message_write(client, message, S_RESPONSE_0);
        }
      }
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_WORK_DATA_PRE);
      break;
    case S_WORK_DATA_PRE:
      // connect to port if needed
      if (client->mode == M_PORT) {
        errno = 0;
        if (connect(client->data_fd, (struct sockaddr*)&client->data_sockaddr, sizeof(struct sockaddr_in)) == -1) {
          if (errno != EINPROGRESS) {
            sprintf(message, "551 %s.", strerror(errno));
            shutdown_all_data_connection(client);
            prepare_cntl_message_write(client, message, S_RESPONSE_1);
            break;
          }
        }
        int ctl_ret = epoll_ctl(client->server->epollfd, EPOLL_CTL_ADD, client->data_fd,
                                &(struct epoll_event){ .data.ptr = client, .events = EPOLLOUT });
        if (ctl_ret == -1) {
          perror("epoll_ctl() in LIST");
          shutdown_all_data_connection(client);
          prepare_cntl_message_write(client, "551 Internal server error.", S_RESPONSE_1);
          break;
        }
      } else {
        if (client->data_fd == -1) break; // stay in this state
      }
      client->state = S_WORK_DATA;
      execute_command(client);
      break;
    case S_WORK_DATA:
      errno = 0;
      if (client->cur_dir_ptr) {
        while (true) {
          client->cur_dir_ent = readdir(client->cur_dir_ptr);
          if (client->cur_dir_ent != NULL &&
            (strcmp(".", client->cur_dir_ent->d_name) == 0 || strcmp("..", client->cur_dir_ent->d_name) == 0)
            ) {
            continue;
          } else {
            break;
          }
        }
        if (errno) {
          perror("LIST/NLST at S_DATA_BUF");
          shutdown_all_data_connection(client);
          prepare_cntl_message_write(client, "451 Failed to list current directory.",
                                     S_RESPONSE_1);
          goto case_cleanup_list_work_data;
        }
        if (client->cur_dir_ent == NULL) {
          shutdown_all_data_connection(client);
          prepare_cntl_message_write(client, "226 Successfully listed contents.",
                                     S_RESPONSE_1);
          goto case_cleanup_list_work_data;
        }
        strcpy(current_filename, client->cur_dir_ent->d_name);
      } else {
        // only a file!
        strcpy(current_filename, client->argument);
      }

      if (client->verb == LIST) {
        strcpy(target, client->cwd);
        strcat(target, "/");
        if (client->cur_dir_ptr) {
          strcat(target, client->argument);
          strcat(target, "/");
        }
        strcat(target, current_filename);
        if (stat(target, &stat_info) == -1) {
          perror("LIST/NLST at stat() of S_DATA_BUF");
          shutdown_all_data_connection(client);
          prepare_cntl_message_write(client, "451 Failed to list current directory.",
                                     S_RESPONSE_1);
          goto case_cleanup_list_work_data;
        }
        strcpy(client->data_write_buf, eplf_line(current_filename, &stat_info));
        strcat(client->data_write_buf, "\r\n");
      } else {
        strcpy(client->data_write_buf, current_filename);
        strcat(client->data_write_buf, "\r\n");
      }

      client->data_bytes_written = 0u;
      client->data_bytes_to_write = strlen(client->data_write_buf);
      client->state = S_DATA_BUF;
      errno = 0;
      epoll_ctl(client->server->epollfd, EPOLL_CTL_MOD, client->data_fd,
                &(struct epoll_event) {.data.ptr = client, .events = EPOLLOUT});
      if (errno) {
        perror("epoll_ctl() in S_WORK_DATA of LIST");
        shutdown_all_data_connection(client);
        prepare_cntl_message_write(client, "451 Failed to list contents.",
                                   S_RESPONSE_1);
        goto case_cleanup_list_work_data;
      }
    case_cleanup_list_work_data:
      break;
    case S_DATA_BUF:
      while (client->data_bytes_written < client->data_bytes_to_write) {
        errno = 0;
        ssize_t delta = write(client->data_fd, client->data_write_buf,
                                            client->data_bytes_to_write - client->data_bytes_written);
        if (errno && !(errno == EAGAIN || errno == EWOULDBLOCK)) {
          fprintf(stderr, "Error writing response: %s\n", strerror(errno));
          shutdown_all_data_connection(client);
          prepare_cntl_message_write(client, "451 Failed to list contents.",
                                     S_RESPONSE_1);
        }
        client->data_bytes_written += delta;
      }
      if (client->data_bytes_written >= client->data_bytes_to_write) {
        if (client->cur_dir_ptr) {
          client->state = S_WORK_DATA;
          execute_command(client);
        } else {
          shutdown_all_data_connection(client);
          prepare_cntl_message_write(client, "226 Successfully listed contents.",
                                     S_RESPONSE_1);
        }
      }
      break;
    case S_RESPONSE_1:
      write_cntl_message(client, S_RESPONSE_END);
      break;
    DEFAULT_MEANS_INVALID_STATE("LIST")
  }
}

void retr_stor_handler(ftp_client_t *client) {
  struct stat stat_info;
  char resolved_path[PATH_MAX], message[BUF_SIZE], read_buf[BUF_SIZE];
  switch (client->state) {
    case S_WORK_RESPONSE_0:
      if (realpath(client->cwd, resolved_path) == NULL) {
        sprintf(message, "550 %s.", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_1);
        goto case_cleanup_work_response_0;
      }
      strcat(resolved_path, "/");
      strcat(resolved_path, client->argument);
      if (!is_valid_path(client->server, resolved_path)) {
        prepare_cntl_message_write(client, "550 Path not allowed or malformed.", S_RESPONSE_1);
        goto case_cleanup_work_response_0;
      }
      if (client->mode == M_UNKNOWN) {
        prepare_cntl_message_write(client, "425 No data connection.", S_RESPONSE_1);
        goto case_cleanup_work_response_0;
      }
      if (client->verb == RETR) {
        if (stat(resolved_path, &stat_info) == -1) {
          sprintf(message, "550 %s.", strerror(errno));
          prepare_cntl_message_write(client, message, S_RESPONSE_1);
        } else {
          sprintf(message, "150 Opening BINARY mode data connection for %s (%ld bytes).",
                  resolved_path, stat_info.st_size);
          prepare_cntl_message_write(client, message, S_RESPONSE_0);
        }
      } else {
        sprintf(message, "150 Opening BINARY mode data connection for %s.",
                resolved_path);
        prepare_cntl_message_write(client, message, S_RESPONSE_0);
      }

    case_cleanup_work_response_0:
      break;
    case S_RESPONSE_0:
      write_cntl_message(client, S_WORK_DATA_PRE);
      break;
    case S_WORK_DATA_PRE:
      // connect to port if needed
      if (client->mode == M_PORT) {
        errno = 0;
        if (connect(client->data_fd, (struct sockaddr*)&client->data_sockaddr, sizeof(struct sockaddr_in)) == -1) {
          if (errno != EINPROGRESS) {
            sprintf(message, "551 %s.", strerror(errno));
            shutdown_all_data_connection(client);
            prepare_cntl_message_write(client, message, S_RESPONSE_1);
            break;
          }
        }
        int ctl_ret = epoll_ctl(client->server->epollfd, EPOLL_CTL_ADD, client->data_fd,
                                &(struct epoll_event){ .data.ptr = client, .events = EPOLLOUT });
        if (ctl_ret == -1) {
          perror("epoll_ctl() in LIST");
          shutdown_all_data_connection(client);
          prepare_cntl_message_write(client, "551 Internal server error.", S_RESPONSE_1);
          break;
        }
      } else {
        if (client->data_fd == -1) {
          // still waiting for connection...
          break;
        }
      }
      client->state = S_WORK_DATA;
      execute_command(client);
      break;
    case S_WORK_DATA:
      client->local_fd = open(client->argument,
                              client->verb == RETR ? O_RDONLY : O_CREAT | O_WRONLY, 0644);
      if (client->local_fd == -1) {
        sprintf(message, "551 %s.", strerror(errno));
        prepare_cntl_message_write(client, message, S_RESPONSE_1);
        goto case_cleanup_retr_work_data;
      }
      if (client->data_fd == -1) {
        prepare_cntl_message_write(client, "425 No data connection.", S_RESPONSE_1);
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
            sprintf(message, "426 Data connection failure: %s.", strerror(errno));
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
        prepare_cntl_message_write(client, "226 Transfer complete.", S_RESPONSE_1);
      }
    case_cleanup_retr_stor_data_sendfile:
      break;
    case S_DATA_BUF:
      while (true) {
        ssize_t read_len = read(client->data_fd, read_buf, BUF_SIZE);
        if (read_len == -1) {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // broken pipe
            sprintf(message, "426 Data connection failure: %s.", strerror(errno));
            prepare_cntl_message_write(client, message, S_RESPONSE_1);
          }
          goto case_cleanup_retr_stor_data_buf;
        } else if (read_len == 0) {
          // closed
          shutdown_all_data_connection(client);
          prepare_cntl_message_write(client, "226 Transfer complete.", S_RESPONSE_1);
          goto case_cleanup_retr_stor_data_buf;
        } else {
          if (write(client->local_fd, read_buf, read_len) == -1) {
            sprintf(message, "552 %s.", strerror(errno));
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
      break;
    DEFAULT_MEANS_INVALID_STATE("RETR")
  }
}

#undef DEFAULT_MEANS_INVALID_STATE