#include <dirent.h>
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

#define MAX_CONNECTIONS 20
#define MAX_BACKLOG 5

struct sockname {
	int sock_fd;
	int type;
	char path[MAXPATH];
	int state;
	mode_t mode;
	char hash[BLOCKSIZE];
	int size;
};

int handle_file(struct sockname *filesrc){
	struct stat fstats_dest;
	char *destpath = malloc(strlen(filesrc->path) + 1);
	destpath[0] = '\0';
	strcat(destpath, filesrc->path);
	int action; 

	// File/dir already exists in destination
	if (lstat(destpath, &fstats_dest) != -1){

		// Emit error if there is a type mismatch
		if (S_ISREG(filesrc->mode) && S_ISREG(fstats_dest.st_mode)){

			// If file sizes are consistent, compare hash to determine
			// if file should be overwritten
			if (fstats_dest.st_size == filesrc->size){
				FILE *filedest;
				filedest = fopen(destpath, "rb");
				if (filedest == NULL){
					perror("fopen");
					return ERROR;
				}else{
					char hashdest[BLOCKSIZE] = {'\0'};
					strcpy(hashdest, hash(hashdest, filedest));
					// If hash is not the same, file in src has changed
					// and must be rewritten to destination
					if (check_hash(filesrc->hash, hashdest) != 0){
						action = SENDFILE;
					} else {
						action = OK;
					}
				
				}
	                        fclose(filedest);

			}else{
				// If size differs, there must be a file transfer
				action = SENDFILE;
			}
		}else if (S_ISDIR(filesrc->mode) && S_ISDIR(fstats_dest.st_mode)){
			action = OK;
		}else{
			return ERROR;
		}

		// If file permissions in src differ from destination,
		// update the destination's permissions
		int permissionsrc = (filesrc->mode & 0777);
		int permissiondest = (fstats_dest.st_mode & 0777);
		if (permissionsrc != permissiondest){
			if (chmod(destpath, permissionsrc) != 0){
				perror("chmod");
			}
		}
		return action;

	// Transfer file if it does not already exist in source
	}else if (lstat(destpath, &fstats_dest) == -1){
		return SENDFILE;
	}else{
		return ERROR;
	}
}

struct request *fill_struct(struct stat fstats, char *src, int type, char *relativesrc){
	FILE *filesrc;
	struct request *file = malloc(sizeof(struct request));

	// Get the file type, path, mode and size (plus hash if reg file)
	file->type = type;
	strcpy(file->path, relativesrc);
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

struct request *handle_copy(char *src, char *relativesrc){
	struct stat fstats;
	struct request *dir_request;
	lstat(src, &fstats);

	// Omit regular files beginning with "."
	if (S_ISREG(fstats.st_mode)) {
		return fill_struct(fstats, src, REGFILE, relativesrc);
	}else if (S_ISDIR(fstats.st_mode)){
		return fill_struct(fstats, src, REGDIR, relativesrc);
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

int transmit_struct(int soc, struct request *file){
	int response = 0;
	int nl_type, nl_mode, nl_size;
	nl_type = htonl(file->type);
	nl_mode = htonl(file->mode);
	nl_size = htonl(file->size);

	write(soc, &nl_type, sizeof(int));
	write(soc, file->path, MAXPATH);
	write(soc, &nl_mode, sizeof(int));
	if (S_ISREG(file->mode)){
		write(soc, file->hash, BLOCKSIZE);
	}
	write(soc, &nl_size, sizeof(int));

	// Get the response from the server on whether the file/dir needs to be copied
	read(soc, &response, sizeof(int));
	return response;
}

int transmit_data(char *source, struct request *file, char *host, unsigned short port){
	int nl_type, nl_mode, nl_size, server_res;

	// If the file needs to be sent, we will fork a new process to open a new connection
	// to the server and perform this procedure.

	FILE *filesrc;
	int soc_child;
	char data;
	establish_connection(&soc_child, host, port);

	int request_type = htonl(TRANSFILE);
	nl_mode = htonl(file->mode);
	nl_size = htonl(file->size);
	write(soc_child, &request_type, sizeof(int));
	write(soc_child, file->path, MAXPATH);
	write(soc_child, &nl_mode, sizeof(int));
	write(soc_child, file->hash, BLOCKSIZE);
	write(soc_child, &nl_size, sizeof(int));

	// Write file contents to server
	if (file->size > 0){
		int bytes_left = file->size;
		int written = 0;
		char contents[file->size];
		FILE *fp = fopen(source, "r");
	        fread(contents, 1, file->size, fp);
                fclose(fp);
		char *ptr = contents;
		// Since we are only gauranteed MAXDATA bytes per read/write calls, 
		// we must make more than one write call from the client to transer
		// a file's contents if it exceeds MAXDATA bytes.
		while (bytes_left > 0) {
			if (bytes_left < MAXDATA) {
                                written = write(soc_child, ptr, bytes_left);
			} else {
				written = write(soc_child, ptr, MAXDATA);
			}
			fseek(fp, ptr - contents, SEEK_SET);
			ptr += written;
			bytes_left -= written;
		}
	}

	read(soc_child, &server_res, sizeof(int));
	if (server_res == OK){
		exit(0);
	} else if (server_res == ERROR){
		printf("Error transferring: %s\n", file->path);
		exit(1);
	}
		
}

int trace_directory(char *source, char *relativesrc, int soc, char *host, unsigned short port){
	struct request *file;
	DIR *dirp = opendir(source);
	if (dirp == NULL) {
		perror("opendir");
	} else {
		struct dirent *dp;
		struct stat fchildstats;
		int server_res, pid;
		int forkcount = 0;
		int exit = 0;
		while ((dp = readdir(dirp)) != NULL) {
			if ((dp->d_name)[0] != '.') {
				// Path is used to store the complete file path to the file
				char *fchildpath = malloc(strlen(source) + strlen(dp->d_name) + 2);
				strcpy(fchildpath, source);
				strcat(fchildpath, "/");
				strcat(fchildpath, dp->d_name);
				lstat(fchildpath, &fchildstats);

				char *frelativepath = malloc(strlen(relativesrc) + strlen(dp->d_name) + 2);
				strcpy(frelativepath, relativesrc);
				strcat(frelativepath, "/");
				strcat(frelativepath, dp->d_name);

				// Ignore links
				if (S_ISREG(fchildstats.st_mode) || S_ISDIR(fchildstats.st_mode)) {
					// File our struct with appropriate file info
					file = handle_copy(fchildpath, frelativepath);
					// Determine if file should be updated on the server
					server_res = transmit_struct(soc, file);
					if (server_res == ERROR) {
						printf("Error transferring: %s\n", fchildpath);
						exit = 1;
					}else if (server_res == SENDFILE && S_ISREG(file->mode)){
						pid = fork();
						if (pid < 0){
							perror("fork");
						} else if (pid == 0){
							// After transmitting file info, we will transmit the data if server requests it
							transmit_data(fchildpath, file, host, port);
						} else{
							forkcount += 1;
						}
					}else if (S_ISDIR(file->mode)) {
						// Recursive call on this file path to process the subdirectory
						trace_directory(fchildpath, frelativepath, soc, host, port);
					}
				}
				free(fchildpath);
				free(frelativepath);
			}
		}// End of processing contents of immediate directory
		if (pid > 0){
			int status, i;
			for (i = 0; i < forkcount; i++){
				if (wait(&status) == -1){
					perror("wait");
				}
				if (WIFEXITED(status)) {
					char exitstatus = WEXITSTATUS(status);
					if (exitstatus == 1){
						exit = 1;
					}
				}
			}
		}
		return exit;
	}
}

int rcopy_client(char *source, char *host, unsigned short port){
	int soc, server_res;
	struct request *file;
	int exit = 0;
	establish_connection(&soc, host, port);

	file = handle_copy(source, basename(source));
	server_res = transmit_struct(soc, file);
	// If we are dealing with a directory, we must traverse its contents
	if (S_ISDIR(file->mode) && server_res != ERROR){
		exit = trace_directory(source, basename(source), soc, host, port);
	}else if (S_ISREG(file->mode) && server_res == SENDFILE){
		exit = transmit_data(source, file, host, port);
	}

	free(file);
	close(soc);
	if (server_res == ERROR) {
		exit = 1;
	}
	return exit;
}

/* 
 * Helper to setup server
 */
int setup(unsigned short port) {
	int on = 1, status;
	struct sockaddr_in self;
	int socket_fd;
	if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	// Make sure we can reuse the port immediately after the
	// server terminates.
	status = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
					  (const char *) &on, sizeof(on));
	if(status == -1) {
		perror("setsockopt -- REUSEADDR");
	}

	self.sin_family = AF_INET;
	self.sin_addr.s_addr = INADDR_ANY;
	self.sin_port = htons(port);
	memset(&self.sin_zero, 0, sizeof(self.sin_zero));  // Initialize sin_zero to 0

	printf("Listening on %d\n", port);

	if (bind(socket_fd, (struct sockaddr *)&self, sizeof(self)) == -1) {
		perror("bind"); // probably means port is in use
		exit(1);
	}

	if (listen(socket_fd, MAX_BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}
	return socket_fd;
}

void rcopy_server(unsigned short port){
	struct request *file;
	FILE *filedest;
	int socket_fd, client_fd;
	int type, nl_mode, size;
	int response;
	int bytes_written = 0;
	struct sockaddr_in peer;
	fd_set all_fds, listen_fds;
	socklen_t socklen;
	struct sockname files[MAX_CONNECTIONS];

	int index;
	for (index = 0; index < MAX_CONNECTIONS; index++) {
		files[index].sock_fd = -1;
		files[index].state = AWAITING_TYPE;
	}

	socket_fd = setup(port);
	int max_fd = socket_fd;
	FD_ZERO(&all_fds);
	FD_SET(socket_fd, &all_fds);

	while (1) {
		listen_fds = all_fds;
		socklen = sizeof(peer);

		int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
		if (nready == -1) {
			perror("server: select");
			exit(1);
		}

		// If socket_fd is ready for I/O => new connection
		if (FD_ISSET(socket_fd, &listen_fds)){

			// Note that we're passing in valid pointers for the second and third
			// arguments to accept here, so we can actually store and use client
			// information.
			if ((client_fd = accept(socket_fd, (struct sockaddr *)&peer, &socklen)) < 0) {
				perror("accept");
			}else{
				printf("New connection on port %d\n", ntohs(peer.sin_port));

				// Add new client_fd to our fd_set to keep track of it
				FD_SET(client_fd, &all_fds);

				// Find next available socket struct in our array and
				// update its fields to reference the new client.
				index = 0;
				while (index < MAX_CONNECTIONS && files[index].sock_fd != -1) {
					index++;
				}
				files[index].sock_fd = client_fd;
				files[index].state = AWAITING_TYPE;

				// We must always update max_fd for the select call
				if (client_fd > max_fd) {
					max_fd = client_fd;
				}
			}
		}

		// Check which client is ready to send information to our server
		int i;
		for (i = 0; i < MAX_CONNECTIONS; i++){
			if (FD_ISSET(files[i].sock_fd, &listen_fds) && files[i].sock_fd > -1){

				if (files[i].state == AWAITING_TYPE){
					// Sanity check to determine if client_fd needs to be removed
					if (read(files[i].sock_fd, &type, sizeof(int)) == 0){
						FD_CLR(client_fd, &all_fds);
						files[i].sock_fd = -1;
					}
					files[i].type = ntohl(type);
					files[i].state = AWAITING_PATH;

				} else if (files[i].state == AWAITING_PATH){
					read(files[i].sock_fd, &(files[i].path), MAXPATH);
					files[i].state = AWAITING_PERM;

				} else if (files[i].state == AWAITING_PERM){
					read(files[i].sock_fd, &nl_mode, sizeof(int));
					files[i].mode = (mode_t) ntohl(nl_mode);

					// Omit hash state if we are dealing with a directory
					if (S_ISREG(files[i].mode)){
						files[i].state = AWAITING_HASH;
					}else {
						files[i].state = AWAITING_SIZE;
					}

				}else if (files[i].state == AWAITING_HASH){
					read(files[i].sock_fd, &(files[i].hash), BLOCKSIZE);
					files[i].state = AWAITING_SIZE;

				} else if (files[i].state == AWAITING_SIZE){
					read(files[i].sock_fd, &size, sizeof(int));
					files[i].size = ntohl(size);

					// printf("File type: %d\n", files[i].type);
					// printf("File path: %s\n", files[i].path);
					// printf("File mode: %d\n", files[i].mode);
					// printf("File size: %d bytes\n", files[i].size);
					if (files[i].type != TRANSFILE){
						response = handle_file(&files[i]);
						if (S_ISREG(files[i].mode)){
								write(files[i].sock_fd, &response, sizeof(int));
								files[i].state = AWAITING_TYPE;
						} else if (S_ISDIR(files[i].mode)){
							if (response == SENDFILE){

								// If directory does not exist on the server create it
								int permissionsrc = (files[i].mode & 0777);
								if (mkdir(files[i].path, permissionsrc) != 0){
									perror("mkdir");
									response = ERROR;
								}else{
									response = OK;
								}
							}
							write(files[i].sock_fd, &response, sizeof(int));
							// If we are dealing with a directory, we must
							// reset the state for this socket to allow for subdirectories/
							// files in the directory to be copied.
							files[i].state = AWAITING_TYPE;
						}

					}else if (files[i].size > 0){
						files[i].state = AWAITING_DATA;
					}else{
						FILE *fp = fopen(files[i].path, "w");
						response = OK;
						if (fp == NULL){
							perror("fopen:");
							response = ERROR;
						}else{
							fclose(fp);
						}
						write(files[i].sock_fd, &response, sizeof(int));
						FD_CLR(files[i].sock_fd, &all_fds);
						files[i].sock_fd = -1;
						files[i].state = AWAITING_TYPE;
					}
					
				}else if (files[i].type == TRANSFILE && files[i].state == AWAITING_DATA){
					int out, sock_index;
					int in = 0;
					FILE *fp = fopen(files[i].path, "w");

					// If the file can be created, update its contents
					if (fp != NULL){
						// Multiple read calls will need to be made if a file contains more than MAXDATA bytes.
						char contents[files[i].size];
						int recieved = 0;
						char *ptr = contents;
						while (in != files[i].size) {
							if ((files[i].size - in) < MAXDATA) {
								recieved = read(files[i].sock_fd, ptr, files[i].size - in);
							} else {
								recieved = read(files[i].sock_fd, ptr, MAXDATA);
							}
							ptr += recieved;
							in += recieved;
						}
						contents[in] = '\0';

						// If we are sure that we have read the entire file contents from 
						// the socket, we will write this to our file on the server and
						// close our socket as it will no longer be used. We must prepare
						// it for reuse when new clients connect to our server.
						out = fwrite(contents, 1, in, fp);

						if (in == out && in == files[i].size){
							response = OK;
						}else{
							response = ERROR;
						}

						// update max_fd
						if (files[i].sock_fd == max_fd){
							sock_index = 0;
							while (sock_index < MAX_CONNECTIONS) {
								if (files[sock_index].sock_fd > max_fd) {
									max_fd = files[sock_index].sock_fd;
								}
								sock_index += 1;
							}
						}
						fclose(fp);
					}else{
						response = ERROR;
					}
					write(files[i].sock_fd, &response, sizeof(int));
					files[i].sock_fd = -1;
					files[i].state = AWAITING_TYPE;
					FD_CLR(files[i].sock_fd, &all_fds);

				}
			}
		}
	}
	close(socket_fd);
}
