#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include "ftree.h"
#include "hash.h"


struct request *fill_struct(struct stat fstats, char *src, int type){
	struct request *file = malloc(sizeof(struct request));
	file->type = type;
	strcpy(file->path, src);
	file->mode = fstats.st_mode;
	file->size = fstats.st_size;

	// Missing: compute hash

	return file;
}

struct request *handle_copy(char *src){
	struct stat fstats;
	lstat(src, &fstats);

	// Omit regular files beginning with "."
	if (S_ISREG(fstats.st_mode) && src[0] != '.') {
		return fill_struct(fstats, src, REGFILE);
	}
}

/* 
 * Establishes connection for client to server
 */
int establish_connection(int *soc, char *host, unsigned short port){
	struct sockaddr_in peer;

	if ((*soc = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("randclient: socket");
		exit(1);
	}

	peer.sin_family = AF_INET;
	peer.sin_port = htons(port);
	if (inet_pton(AF_INET, host, &peer.sin_addr) < 1) {
		perror("randclient: inet_pton");
		close(*soc);
		exit(1);
	}

	if (connect(*soc, (struct sockaddr *)&peer, sizeof(peer)) == -1) {
		perror("randclient: connect");
		exit(1);
	}
}

int rcopy_client(char *source, char *host, unsigned short port){
	int soc, nl_type, nl_mode, nl_size;
	struct request *file;
	char *message = malloc(strlen(source) + 3);
	strncpy(message, source, strlen(source));
	strcat(message, "\r\n");

	establish_connection(&soc, host, port);

	file = handle_copy(basename(source));
	nl_type = htonl(file->type);
	nl_mode = htonl(file->mode);
	nl_size = htonl(file->size);

	write(soc, &nl_type, sizeof(int));
	write(soc, file->path, MAXPATH);
	write(soc, &nl_mode, sizeof(mode_t));
	// Missing: Transmit hash
	write(soc, &nl_size, sizeof(int));

	close(soc);
	return 0;
}

/* 
 * Helper to setup server
 */
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
	int inbuf; // how many bytes currently in buffer?
	int room; // how much room left in buffer?
	char *after; // pointer to position after the (valid) data in buf

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
			int type, nl_mode, size;
			char path[MAXPATH];
			mode_t mode;

			// Pointer to modify path name for read call
			after = path;        // start writing at beginning of path

			read(fd, &type, sizeof(int));
			type = ntohl(type);
			read(fd, after, MAXPATH);
			read(fd, &nl_mode, sizeof(mode_t));
			mode = (mode_t) ntohl(nl_mode);
			// Missing: read hash
			read(fd, &size, sizeof(mode_t));
			size = ntohl(size);

			printf("File type: %d\n", type);
			printf("File path: %s\n", path);
			printf("File mode: %d\n", mode);
			// Missing: print hash
			printf("File size: %d bytes\n", size);
		}
		close(fd);
	}
}
