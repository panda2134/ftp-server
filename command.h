#ifndef REQUEST_H
#define REQUEST_H

#include "client.h"

typedef enum request_verb {
  QUIT, ABOR, SYST, PASV, PWD,
  USER, PASS, RETR, STOR, TYPE, PORT, MKD, CWD,
  LIST, NLST, RMD, RNFR, RNTO, DELE, CDUP,

  UNKNOWN_VERB, INIT,
  NUM_REQUEST_VERB
} request_verb;

struct ftp_client_t;
typedef struct ftp_client_t ftp_client_t;

void quit_handler(ftp_client_t *client);
void syst_handler(ftp_client_t *client);
void pasv_handler(ftp_client_t *client);
void pwd_handler(ftp_client_t *client);
void user_handler(ftp_client_t *client);
void pass_handler(ftp_client_t *client);
void type_handler(ftp_client_t *client);
void port_handler(ftp_client_t *client);
void mkd_handler(ftp_client_t *client);
void cwd_handler(ftp_client_t *client);
void list_handler(ftp_client_t *client);
void rmd_handler(ftp_client_t *client);
void rnfr_handler(ftp_client_t *client);
void rnto_handler(ftp_client_t *client);
void dele_handler(ftp_client_t *client);
void cdup_handler(ftp_client_t *client);
void retr_stor_handler(ftp_client_t *client);
void unknown_handler(ftp_client_t *client);
void init_handler(ftp_client_t *client);

/**
 * try to read more into the command buffer. If the part that was just read contains `\\r\\n`, the complete command
 * would be copied into the `ftp_client_t.cur_cmd` array, while its previous content would be moved into `prev_cmd`
 * array.
 *
 * Upon every read(), the buffer availability is checked.
 * In case of a full command buffer, its content gets cleared before this read. Last, the read part is removed from
 * the buffer, and bytes_read -= length of (cur_cmd) accordingly.
 *
 * @param client the client to work on
 * @return whether a complete command is ready & prepared in cur_cmd[]
 */
bool read_command_buf(ftp_client_t *client);

/**
 * Parse the command line, and return which handler should be called.
 * in case of no valid handler, UNKNOWN_VERB is returned for error handling
 */
void parse_command(ftp_client_t *client);

/**
 * Process the current command according to the state.
 * @param client the client to work on
 */
void execute_command(ftp_client_t *client);

/**
 * Copy the string in str into write buffer of control connection, append '\\r\\n',
 * set content length accordingly, and set state to new_state;
 * @param client the client to work on
 * @param str message
 * @param response_state switched onto the given state after copying to buf.
 */
void prepare_cntl_message_write(ftp_client_t* client, const char* str, int response_state);

/**
 * Write response on control connection.
 * @return true if fully written
 * @param work_state switched onto the given state after copying to buf.
 */
bool write_cntl_message(ftp_client_t* client, int work_state);

#endif