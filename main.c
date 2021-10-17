#include <stdio.h>
#include <string.h>
#include <argp.h>
#include <limits.h>
#include "server.h"


struct arguments {
  int port;
  char if_prefix[256], root[PATH_MAX];
};

static char doc[] = "A simple FTP server.";
static struct argp_option options[] = {
    {"port", 'p', "PORT", 0, "The port that server listens on (default to 21)"},
    {"root", 'r', "ROOT", 0, "The root area of the server. (default to /tmp/)"},
    {"interface", 'i', "INTERFACE", 0, "Interface (prefix) to which the server will listen"
                                                                  "default to lo"},
    { 0 }
};

ftp_server_t* volatile server;

static error_t parse_opt (int key, char *arg, struct argp_state *state) {
  static char buf[PATH_MAX];
  struct arguments *rtn = state->input;
  unsigned long port;

  if (state->arg_num != 0)
    argp_usage(state);

  switch (key) {
    case 'p':
      errno = 0;
      port = strtoul(arg, NULL, 10);
      if (errno || port < 0 || port > UINT16_MAX) {
        argp_usage(state);
      } else {
        rtn->port = (uint16_t)port;
      }
      break;
    case 'r':
      strcpy(rtn->root, arg);
      break;
    case 'i':
      strcpy(rtn->if_prefix, arg);
      break;
    default:
      return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

int main(int argc, char **argv) {
  struct arguments args;
  args.port = 21;
  strcpy(args.root, "/tmp/");
  strcpy(args.if_prefix, INTERFACE_PREFIX);

  char** argv_copy = malloc(sizeof(char*) * argc);
  for (int i = 0; i < argc; i++) {
    ssize_t len = strlen(argv[i]);
    argv_copy[i] = malloc(len + 5);
    if (argv[i][0] == '-' && argv[i][1] != '-' 
        && len > 2) {
      strcpy(argv_copy[i], "-");
      strcat(argv_copy[i], argv[i]);
    } else {
      strcpy(argv_copy[i], argv[i]);
    }
  }

  argp_parse(&(struct argp){ options, parse_opt, "", doc }, argc, argv_copy, 0, 0, &args);

  puts("Launching FTP server...");
  server = create_ftp_server(args.port, args.root, args.if_prefix);
  server_loop(server);
}
