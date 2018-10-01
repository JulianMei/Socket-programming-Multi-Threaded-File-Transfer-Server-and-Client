#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>


#include "gfserver.h"
#include "gfserver-student.h"

typedef ssize_t (*callback)(gfcontext_t *, const char *, void*);

struct gfserver_t
{
	unsigned short port;
	int maxpending;
	callback handler;
	void* handlerarg;
};

struct gfcontext_t
{
	int sd;       // which client this request responds to
	int status;   // Request parse status
};

/* 
 * Modify this file to implement the interface specified in
 * gfserver.h.
 */

void gfs_abort(gfcontext_t *ctx){
    close(ctx->sd);
}

ssize_t gfs_send(gfcontext_t *ctx, const void *data, size_t len){
    // Send data until completion
	int requestLen = len;  // Cleanup: bytes sent may need to + 1
	int bytesLeft = requestLen;
	int totalSent = 0; 
	int bytesSent = 0;
	while (totalSent < requestLen) {
		bytesSent = send(ctx->sd, data + totalSent, bytesLeft, 0);
		if (bytesSent < 0) { break; }
		totalSent += bytesSent;
		bytesLeft -= bytesSent;
	}
	if(bytesSent < 0) {
		fprintf(stderr, "Failed to send Header! \n");  // REMOVE: DEBUG_INFO
        // Cleanup: what should be the status when request sending fail?
	} 

	fprintf(stdout, "Total sent: %d \n", totalSent);  // REMOVE: DEBUG_INFO
    return totalSent;
}

ssize_t gfs_sendheader(gfcontext_t *ctx, gfstatus_t status, size_t file_len){
	// Sends the header response until completion.
	// 1. Wrap up a header string
	char HdrStr[64];
	memset(HdrStr, 0, sizeof(HdrStr));
    const char *statusStr = "UNKNOWN";
    switch (status) {
        case GF_OK: {
            statusStr = "OK";
        }
        break;
        
        case GF_ERROR: {
            statusStr = "ERROR";
        }
        break;

        case GF_FILE_NOT_FOUND: {
            statusStr = "FILE_NOT_FOUND";
        }
        break;

        case GF_INVALID: {
            statusStr = "INVALID";
        }
        break;

		default:
		break;
    }

	char lenStr[16];
	memset(lenStr, 0 ,sizeof(lenStr));
    sprintf(lenStr, "%zu", file_len);
    sprintf(HdrStr, "GETFILE %s %s \r\n\r\n", statusStr, lenStr);
	fprintf(stdout, "Header string: %s \n", HdrStr);  // REMOVE: DEBUG_INFO

    // 2. Send header until completion
	int requestLen = strlen(HdrStr);  // Cleanup: bytes sent may need to + 1
    fprintf(stdout, "Header Length: %d \n", requestLen);  // REMOVE: DEBUG_INFO
	int bytesLeft = requestLen;
	int totalSent = 0; 
	int bytesSent = 0;
	while (totalSent < requestLen) {
		bytesSent = send(ctx->sd, HdrStr + totalSent, bytesLeft, 0);
		if (bytesSent < 0) { break; }
		totalSent += bytesSent;
		bytesLeft -= bytesSent;
	}
	if(bytesSent < 0) {
		fprintf(stderr, "Failed to send Header! \n");  // REMOVE: DEBUG_INFO
        // Cleanup: what should be the status when request sending fail?
	} 

	fprintf(stdout, "Total sent: %d \n", totalSent);  // REMOVE: DEBUG_INFO
    return totalSent;
}

gfserver_t* gfserver_create() {
	void *server = malloc(sizeof(struct gfserver_t));
	memset(server, 0, sizeof(struct gfserver_t));
	return (struct gfserver_t *)server;
}

void gfserver_serve(gfserver_t *gfs){
	// 1. Start server and wait for connection
	int sd, addrStat;
	struct addrinfo hints, *servInfo;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;  // use localhost
	char portStr[16];
	memset(portStr, 0, sizeof(portStr));
	sprintf(portStr, "%d", gfs->port);

	if ((addrStat = getaddrinfo(NULL, portStr, &hints, &servInfo)) != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(addrStat));
		exit(-1);
	}

	if ((sd = socket(servInfo->ai_family, servInfo->ai_socktype, servInfo->ai_protocol)) < 0) {
		fprintf(stderr, "Failed to create socket! \n");
		exit(-1);
	}

	int yes = 1;
	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
		perror("setsockopt");
		exit(-1);
	}

	if (bind(sd, servInfo->ai_addr, servInfo->ai_addrlen) < 0) {
		fprintf(stderr, "Failed to bind! \n");
		exit(-1);
	}

	listen(sd, gfs->maxpending);

	struct sockaddr_storage client;
	socklen_t socksize = sizeof(client);
	char ReqStr[MAX_REQUEST_LEN];
	while (1)
	{
		memset(ReqStr, 0, sizeof(ReqStr));
		fprintf(stdout, "Waiting for connection...\n");  // DEBUG_PRINT
		int cd = accept(sd, (struct sockaddr*)&client, &socksize);
		if (cd < 0) {
			fprintf(stderr, "Failed to accept! \n");
			continue;
		}

		struct gfcontext_t context;
		memset(&context, 0, sizeof(context));
		context.sd = cd;

		// 2. Receive request from client until completion, up to [sizeof(ReqStr) - 1] bytes
		int bytesRecv = 0;
	    int totalRecv = 0;
		while((bytesRecv = recv(cd, ReqStr + totalRecv, sizeof(ReqStr) - 1 - totalRecv, 0)) > 0) {
			totalRecv += bytesRecv;
			ReqStr[totalRecv] = '\0';  // Add '\0' temporily, the next receive will overwrite this byte, or this will be the last receive.
			if(strstr(ReqStr, "\r\n\r\n") != NULL) { 
				break;  // Break if found the end marker
			}  
		}
		if(bytesRecv < 0) {
			fprintf(stderr, "Failed to receive! \n");
			ackInvalidError(cd, "ERROR");  // Acknowledge ERROR
			continue;
		}
		ReqStr[totalRecv] = '\0';  // Should not assume received request is null-terminated.
		
		// 3. Parse the request and call handler to process it or acknowledge invalid.
        char* tokens[4];  // to store tokens extracted from request string, note this will modify ReqStr
	    int index = 0;
	    char* token = strtok(ReqStr, " ");
	    while (token) {
		    fprintf(stdout, "Request token %d: %s\n", index, token);  // DEBUG_PRINT
		    tokens[index++] = token;
		    token = strtok(NULL, " ");
	    }

		if ((strcmp(tokens[0],"GETFILE") != 0) || (strcmp(tokens[1],"GET") != 0) || tokens[2][0] != '/') {
			ackInvalidError(cd, "INVALID");
			continue;
	    }
		
		callback requestHdlr = gfs->handler;
		if((*requestHdlr)(&context, tokens[2], gfs->handlerarg) < 0)
		{
			fprintf(stderr, "Handler execution failed, connection may have been aborted! \n");
		}
		
		close(context.sd);
	}
}

void gfserver_set_handlerarg(gfserver_t *gfs, void* arg){
	gfs->handlerarg = arg;
}

void gfserver_set_handler(gfserver_t *gfs, ssize_t (*handler)(gfcontext_t *, const char *, void*)){
	gfs->handler = handler;
}

void gfserver_set_maxpending(gfserver_t *gfs, int max_npending){
	gfs->maxpending = max_npending;
}

void gfserver_set_port(gfserver_t *gfs, unsigned short port){
	gfs->port = port;
}

void ackInvalidError(int socket, char* status) {
    char HdrMsg[64];  // cleanup
	memset(HdrMsg, 0 , sizeof(HdrMsg));
    sprintf(HdrMsg, "GETFILE %s 0 \r\n\r\n", status);

    // Send header until completion
	int requestLen = strlen(HdrMsg);  // Cleanup: bytes sent may need to + 1
	int bytesLeft = requestLen;
	int totalSent = 0; 
	int bytesSent = 0;
	while (totalSent < requestLen) {
		bytesSent = send(socket, HdrMsg + totalSent, bytesLeft, 0);
		if (bytesSent < 0) { break; }
		totalSent += bytesSent;
		bytesLeft -= bytesSent;
	}
	if(bytesSent < 0) {
		fprintf(stderr, "Failed to send Header! \n");  // REMOVE: DEBUG_INFO
		//ackInvalidError(socket, "ERROR");  // If this ACK failed, Re-ACK ERROR msg.
 	} 
}




