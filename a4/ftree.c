#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "ftree.h"

int rcopy_client(char *source, char *host, unsigned short port){
  int soc;
  char message[18] = "A stitch in time\r\n";
  struct sockaddr_in peer;

  int current_byte, bytes_left, total_bytes, howmany;

  if ((soc = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("randclient: socket");
    exit(1);
  }

  peer.sin_family = AF_INET;
  peer.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &peer.sin_addr) < 1) {
    perror("randclient: inet_pton");
    close(soc);
    exit(1);
  }

  if (connect(soc, (struct sockaddr *)&peer, sizeof(peer)) == -1) {
    perror("randclient: connect");
    exit(1);
  }

  write(soc, message, strlen(message));

  close(soc);
  return 0;
}

int setup(unsigned short port) {
  int on = 1, status;
  struct sockaddr_in self;
  int listenfd;
  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    exit(1);
  }

  // Make sure we can reuse the port immediately after the
  // server terminates.
  status = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
                      (const char *) &on, sizeof(on));
  if(status == -1) {
    perror("setsockopt -- REUSEADDR");
  }

  self.sin_family = AF_INET;
  self.sin_addr.s_addr = INADDR_ANY;
  self.sin_port = htons(port);
  memset(&self.sin_zero, 0, sizeof(self.sin_zero));  // Initialize sin_zero to 0

  printf("Listening on %d\n", port);

  if (bind(listenfd, (struct sockaddr *)&self, sizeof(self)) == -1) {
    perror("bind"); // probably means port is in use
    exit(1);
  }

  if (listen(listenfd, 5) == -1) {
    perror("listen");
    exit(1);
  }
  return listenfd;
}

void rcopy_server(unsigned short port){
  int listenfd;
  int fd, nbytes;
  char buf[30];
  int inbuf; // how many bytes currently in buffer?
  int room; // how much room left in buffer?
  char *after; // pointer to position after the (valid) data in buf
  int where; // location of network newline

  struct sockaddr_in peer;
  socklen_t socklen;

  listenfd = setup(port);
  while (1) {
    socklen = sizeof(peer);
    // Note that we're passing in valid pointers for the second and third
    // arguments to accept here, so we can actually store and use client
    // information.
    if ((fd = accept(listenfd, (struct sockaddr *)&peer, &socklen)) < 0) {
      perror("accept");

    } else {
      printf("New connection on port %d\n", ntohs(peer.sin_port));

      // Receive messages
      inbuf = 0;          // buffer is empty; has no bytes
      room = sizeof(buf); // room == capacity of the whole buffer
      after = buf;        // start writing at beginning of buf

      nbytes = read(fd, after, room);

      printf("Next message: %s", buf);
       
      }
      close(fd);
    }
  }
