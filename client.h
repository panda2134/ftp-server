#ifndef FTP_SERVER_CLIENT_H
#define FTP_SERVER_CLIENT_H

#include <stdlib.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "server.h"
#include "command.h"

typedef enum {
  M_UNKNOWN, M_PORT, M_PASV
} connection_mode;

enum client_state {
  S_INIT, S_CMD,
  S_WORK_RESPONSE_0, S_RESPONSE_0,
  S_WORK_DATA_PRE, S_WORK_DATA,
  S_DATA_BUF, S_DATA_SENDFILE,
  S_RESPONSE_1,
  S_RESPONSE_END,
  S_QUIT
};

struct ftp_server_t;

typedef struct ftp_client_t {
  /** state of the state machine*/
  int state;
  /** PORT: client to server, PASV: server to client. */
  connection_mode mode;
  /** socket data for cntl / data, and a local file */
  int cntl_fd, data_fd, pasv_listen_fd, local_fd;
  struct sockaddr_in cntl_sockaddr, data_sockaddr;
  socklen_t cntl_sockaddr_len, data_sockaddr_len;
  /** current working directory. */
  char cwd[PATH_MAX];
  /** username. NULL for unauthorized users. */
  char username[USERNAME_LENGTH];
  /** control request buffer. */
  char cntl_read_buf[BUF_SIZE];
  ssize_t cntl_bytes_read;
  /** control response buffer. */
  char cntl_write_buf[BUF_SIZE];
  size_t cntl_bytes_written, cntl_bytes_to_write;
  /** data response buffer. */
  char data_write_buf[BUF_SIZE];
  size_t data_bytes_written, data_bytes_to_write;
  /** current and last commands (0-terminating) */
  char last_cmd[BUF_SIZE + 1], cur_cmd[BUF_SIZE + 1];
  /** parsed command type */
  int verb, last_verb;
  /** parsed argument pointer */
  char argument[BUF_SIZE + 1], last_argument[BUF_SIZE + 1];
  /** lengths */
  ssize_t cmd_len, last_cmd_len, arg_len, last_arg_len;
  /** whether the last command failed. */
  bool last_failed;
  /** cwd directory pointer for LIST */
  DIR* cur_dir_ptr;
  /** currently traversed dirent */
  struct dirent* cur_dir_ent;

  /** the server that client belongs to */
  struct ftp_server_t* server;
} ftp_client_t;

void create_client(ftp_client_t*, struct ftp_server_t*);
void update_client(ftp_client_t*);
void try_accept_pasv(ftp_client_t*);

#endif