# ftp-server

## Pseudo code

1. Loop through available fds
2. check the type of fd:
   - `listen_sock_cntl`: accept the connection and watch the `conn_sock`
   - `conn_sock_cntl`:
     - work on the previous request, until EAGAIN
     - read the request into buffer, until EAGAIN; on full buffer, clear it
       - when `\r\n` is detected, try to handle the request
   - `listen_sock_data`: accept the new PASV connection
   - `conn_sock_data`: no matter of PASV or PORT; 
     - check for upload / download

## Commands and implementation

Note: CMD sets last_fail according to status code, and goto CMD.

### Part 1

- USER:
  - WORK_RESPONSE_0:
    - if not anonymous then set fail msg
  - RESPONSE_0:
    - try to send response; on fully written, goto CMD
    - if fail == true: goto CMD
- PASS:
  - WORK_RESPONSE_0:
    - reject if last_cmd is not a USER or last failed
  - RESPONSE_0:
    - try to send response; on fully written, goto CMD
- RNFR:
  - WORK_RESPONSE_0:
    - 450 on ENOENT, 350 otherwise
  - RESPONSE_0:
    - try to send response; on fully written, goto CMD
- RNTO:
  - WORK_RESPONSE_0:
    - reject with 503 if last is not RNFR or last failed
  - RESPONSE_0:
    - try to send response; on fully written, goto CMD
- CWD / MKD / RMD / DELE:
  - WORK_RESPONSE_0:
    - check: root + 'path' inside root & path exists
    - generate response
  - RESPONSE_0:
    - try to send response; on fully written, goto CMD
- SYST:
  - WORK_RESPONSE_0:
    - generate response directly: `215 UNIX Type: L8`
  - RESPONSE_0:
    - try to send response; on fully written, goto CMD
- TYPE:
  - WORK_RESPONSE_0:
    - generate response directly:
      - `200 Type set to I.` if `TYPE I`
      - `504 Unsupported type` otherwise
  - RESPONSE_0:
    - try to send response; on fully written, goto CMD
- PWD:
  - WORK_RESPONSE_0:
    - generate response directly
  - RESPONSE_0:
    - try to send response; on fully written, goto CMD
- QUIT:
  - WORK_RESPONSE_0:
    - goto QUIT directly
    
### Part 2

- PASV:
  - WORK_RESPONSE_0
    - close current data connection if any;
    - if current mode is pasv, unregister pasv listen fd and close it
    - then create a new socket
    - bind it, listen to it (with only 1 allowed client)
      and register it with epoll (EPOLLIN); set to `nonblock` for `accept()`
    - set mode to PASV
    - make response text
  - RESPONSE_0:
    - try to send response; on fully written, goto CMD
  - **Note**: try accepting a new data connection iff
    - mode is pasv
    - data connection filedes is -1
    - then epoll read on it
- PORT:
  - WORK_RESPONSE_0:
    - close current data connection if any;
    - if current mode is pasv, unregister pasv listen fd and close it
    - then create a new nonblocking socket, but do not connect
    - save the client sockaddr (remember `htonl` etc.) for further use
  - RESPONSE_0:
    - try to send response; on fully written, goto CMD
- LIST:
  - WORK_RESPONSE_0:
    - `fstat` on `cwd` (or arg if any)
    - generate cntl output
  - RESPONSE_0:
    - try to send cntl response; on fully written, goto WORK_DATA
  - WORK_DATA:
    - prepare `DIR*`
    - generate `ls`-like output, write to data buf
    - goto DATA_BUF
  - DATA_BUF:
    - try to send data from buf, while generating more data;
      - on fully written, close the connection(-1) and generate 226 write buf on cntl, then goto RESPONSE_1
  - RESPONSE_1:
    - try writing the buf of cntl; on fully written, goto CMD
- RETR:
  - WORK_RESPONSE_0:
    - if mode is PASV, check if the data connection is made
    - otherwise, generate message for PORT
    - check fcntl
  - RESPONSE_0:
    - send response and continue if all done
    - on all done:
      - if response is ok,
        - if mode is PORT
          - `connect()`
          - epoll -> wait on writable
          - goto WORK_DATA, and return
        - else
          - epoll -> wait on writable
      - else:
        - goto CMD
  - WORK_DATA:
    - open file fd
    - if fails, check close, make error response & goto RESPONSE_1 
  - DATA_SENDFILE:
    - try `sendfile()`, until done
    - if done, close 2x data conn + unreg epoll & generate response & goto RESPONSE_1
    - if error, close 2x data conn + unreg epoll, generate new response on cntl & goto RESPONSE_1
  - RESPONSE_1
    - send all response, on done, goto CMD
- STOR:
  - WORK_RESPONSE_0:
    - if mode is PASV, check if the data connection is made
    - otherwise, generate message for PORT
    - check fcntl
  - RESPONSE_0:
    - send response and continue if all done
    - on all done:
      - if response is ok,
        - if mode is PORT
        - `connect()`
        - epoll -> wait on read
        - goto WORK_DATA, and return
      - else:
        - goto CMD
  - WORK_DATA:
    - open file fd
    - if fails, check close, make error response & goto RESPONSE_1 
  - DATA_SENDFILE:
    - try read into buffer, write file, until done
    - if done, close 2x data conn + unreg epoll & generate response & goto RESPONSE_1
    - if error, close 2x data conn + unreg epoll, generate new response on cntl & goto RESPONSE_1
  - RESPONSE_1
    - send all response, on done, goto CMD


## error handling

- [ ] epoll on HUP of control connection, and close data connection accordingly
- [ ] TELNET strings

## use a state machine for each client

    For each command or command sequence there are three possible
    outcomes: success (S), failure (F), and error (E).  In the state
    diagrams below we use the symbol B for "begin", and the symbol W for
    "wait for reply".

               ------------------------------------
              |                                    |
      Begin   |                                    |
        |     V                                    |
        |   +---+  cmd   +---+ 2         +---+     |
         -->|   |------->|   |---------->|   |     |
            |   |        | W |           | S |-----|
         -->|   |     -->|   |-----      |   |     |
        |   +---+    |   +---+ 4,5 |     +---+     |
        |     |      |    | |      |               |
        |     |      |   1| |3     |     +---+     |
        |     |      |    | |      |     |   |     |
        |     |       ----  |       ---->| F |-----
        |     |             |            |   |
        |     |             |            +---+
         -------------------
              |
              |
              V
             End
      