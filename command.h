#ifndef REQUEST_H
#define REQUEST_H

#include "client.h"

typedef enum {
  QUIT, ABOR, SYST, PASV, PWD,
  USER, PASS, RETR, STOR, TYPE, PORT, MKD, CWD,
  LIST, RMD, RNFR, RNTO, DELE,

  UNKNOWN_VERB,
  NUM_REQUEST_VERB
} request_verb;

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
void retr_stor_handler(ftp_client_t *client);
void unknown_handler(ftp_client_t *client);

void (*VERB_HANDLER[NUM_REQUEST_VERB])(ftp_client_t *) = {
    [QUIT] = quit_handler, [ABOR] = quit_handler, [SYST] = syst_handler,
    [PASV] = pasv_handler, [PWD] = pwd_handler, [USER] = user_handler, [PASS] = pass_handler,
    [RETR] = retr_stor_handler, [STOR] = retr_stor_handler, [TYPE] = type_handler, [PORT] = port_handler,
    [MKD] = mkd_handler, [CWD] = cwd_handler, [LIST] = list_handler, [RMD] = rmd_handler,
    [RNFR] = rnfr_handler, [RNTO] = rnto_handler, [DELE] = dele_handler,
    [UNKNOWN_VERB] = unknown_handler
};

const char *VERB_STR[NUM_REQUEST_VERB] = {
    [QUIT] = "QUIT", [ABOR] = "ABOR", [SYST] = "SYST", [PASV] = "PASV", [PWD] = "PWD",
    [USER] = "USER", [PASS] = "PASS", [RETR] = "RETR", [STOR] = "STOR", [TYPE] = "TYPE",
    [PORT] = "PORT", [MKD] = "MKD", [CWD] = "CWD", [LIST] = "LIST", [RMD] = "RMD",
    [RNFR] = "RNFR", [RNTO] = "RNTO", [DELE] = "DELE", [UNKNOWN_VERB] = ""
};

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
void prepare_cntl_message_write(ftp_client_t* client, const char* str, client_state response_state);

/**
 * Write response on control connection.
 * @return true if fully written
 * @param work_state switched onto the given state after copying to buf.
 */
bool write_cntl_message(ftp_client_t* client, client_state work_state);

#endif