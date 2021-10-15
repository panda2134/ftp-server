#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include "server.h"

ftp_server_t* volatile server;

void on_termination(int dummy) {
  close(server->cntl_listen_fd);
}

int main() {
  puts("Launching FTP server...");
  signal(SIGINT, on_termination);
  signal(SIGTERM, on_termination);
  server = create_ftp_server(10022, "/tmp/ftp/");
  server_loop(server);
}
