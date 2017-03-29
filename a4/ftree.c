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


int handle_regular_file(struct request *filesrc){
	struct stat fstats_dest;
	char *destpath = malloc(strlen(filesrc->path) + 1);
	destpath[0] = '\0';
	strcat(destpath, filesrc->path);

	// File/dir already exists in destination
	if (lstat(destpath, &fstats_dest) != -1 && S_ISREG(fstats_dest.st_mode)){

		// If file sizes are consistent, compare hash to determine
		// if file should be overwritten
		if (fstats_dest.st_size == filesrc->size){
			FILE *filedest;
			filedest = fopen(destpath, "rb");

			// Emit error if there is a type mismatch
			if (filedest == NULL){
	    		perror("fopen");
	    		return ERROR;
	    	}else{

	    		char hashdest[BLOCKSIZE];
	    		strcpy(hashdest, hash(hashdest, filedest));
	    		fclose(filedest);

	    		// If hash is not the same, file in src has changed
	    		// and must be rewritten to destination
	    		if (check_hash(filesrc->hash, hashdest) != 0){
	    			return SENDFILE;
	    		}
	    		return OK;
	    	}

	    // If size differs, copy is performed
		}else{
			// Copy file contents to destination
			return SENDFILE;

		} // End of file size comparison

		// If file permissions in src differ from destination,
		// update the destination's permissions
		int permissionsrc = (filesrc->mode & 0777);
		int permissiondest = (fstats_dest.st_mode & 0777);
		if (permissionsrc != permissiondest){
			if (chmod(destpath, permissionsrc) != 0){
				perror("chmod");
			}

		}

	// Create file since it does not already exist in source
	}else if (lstat(destpath, &fstats_dest) == -1){
		return SENDFILE;
	}else{
		return ERROR;
	}
}

struct request *fill_struct(struct stat fstats, char *src, int type){
	FILE *filesrc;
	struct request *file = malloc(sizeof(struct request));
	file->type = type;
	strcpy(file->path, src);
	file->mode = fstats.st_mode;
	file->size = fstats.st_size;
    filesrc = fopen(src, "r");
	if (filesrc == NULL){
	    perror("fopen");
	}else{
		strcpy(file->hash, hash(file->hash, filesrc));
	}
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
	int soc, nl_type, nl_mode, nl_size, response;
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
	write(soc, file->hash, BLOCKSIZE);
	write(soc, &nl_size, sizeof(int));

	read(soc, &response, sizeof(int));
	printf("RESPONSE FROM SERVER: %d\n", response);
	free(file);
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
	int listenfd, fd;
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
			struct request *file = malloc(sizeof(struct request));
			int type, nl_mode, size;
			char path[MAXPATH];
			int response;

			// Read from client to fill request struct
			read(fd, &type, sizeof(int));
			file->type = ntohl(type);
			read(fd, &(file->path), MAXPATH);
			read(fd, &nl_mode, sizeof(mode_t));
			file->mode = (mode_t) ntohl(nl_mode);
			read(fd, &(file->hash), BLOCKSIZE);
			read(fd, &size, sizeof(mode_t));
			file->size = ntohl(size);

			// printf("File type: %d\n", file->type);
			// printf("File path: %s\n", file->path);
			// printf("File mode: %d\n", file->mode);
			// printf("File size: %d bytes\n", file->size);

			if (file->type == REGFILE){
				response = handle_regular_file(file);
				write(fd, &response, sizeof(int));
			}
		}
		close(fd);
	}
}
