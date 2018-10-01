#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include "gfclient.h"
#include "gfclient-student.h"

#define GENERAL_BUFF_SIZE  64
#define RECV_CHUNK_SIZE    8192

typedef void(*callback)(void *, size_t, void *);

// Define the concrete gfcrequest_t structure.
struct gfcrequest_t
{
    gfstatus_t status;
    size_t bytesRecv;
    size_t fileLength;
	char server[GENERAL_BUFF_SIZE];
	unsigned short port;
	char path[GENERAL_BUFF_SIZE];
	callback headerfunc;
	callback writefunc;
	void*    headerarg;
	void*    writearg;
};

// optional function for cleaup processing.
void gfc_cleanup(gfcrequest_t *gfr){
	free(gfr);
    gfr = NULL;
}

gfcrequest_t *gfc_create(){
	void* req = malloc(sizeof(struct gfcrequest_t));
	memset(req, 0, sizeof(struct gfcrequest_t));
    return (struct gfcrequest_t *)req;
}

size_t gfc_get_bytesreceived(gfcrequest_t *gfr){
    return gfr->bytesRecv;
}

size_t gfc_get_filelen(gfcrequest_t *gfr){
    return gfr->fileLength;
}

gfstatus_t gfc_get_status(gfcrequest_t *gfr){
    return gfr->status;
}

void gfc_global_init(){
}

void gfc_global_cleanup(){
}

int gfc_perform(gfcrequest_t *gfr){
	// 1. Establish connection
	int sd, addrStat, connectStat;
	struct addrinfo hints, *servInfo;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	char portStr[16];
	memset(portStr, 0, sizeof(portStr));
	sprintf(portStr, "%d", gfr->port);
	if ((addrStat = getaddrinfo(gfr->server, portStr, &hints, &servInfo)) != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(addrStat));  // REMOVE: DEBUG_INFO
		return -1;
	}

	if ((sd = socket(servInfo->ai_family, servInfo->ai_socktype, servInfo->ai_protocol)) < 0) {
		fprintf(stderr, "Failed to create socket! \n");  // REMOVE: DEBUG_INFO
		return -1;
	}

	if ((connectStat = connect(sd, servInfo->ai_addr, servInfo->ai_addrlen)) < 0) {
		fprintf(stderr, "Failed to connect! \n");  // REMOVE: DEBUG_INFO
        freeaddrinfo(servInfo);
		return -1;
	}
	freeaddrinfo(servInfo);

	// 2. Sent file transfer request to server
	char ReqStr[1024];
	memset(ReqStr, 0, sizeof(ReqStr));
	sprintf(ReqStr, "GETFILE GET %s \r\n\r\n", gfr->path);
	fprintf(stdout, " Requested string is %s\n", ReqStr);  // REMOVE: DEBUG_INFO

	// Send until completion
	int requestLen = strlen(ReqStr);  // We don't send '\0' to server.
	int bytesLeft = requestLen;
	int totalSent = 0; 
	int bytesSent = 0;
	while (totalSent < requestLen) {
		bytesSent = send(sd, ReqStr + totalSent, bytesLeft, 0);
		if (bytesSent < 0) { break; }
		totalSent += bytesSent;
		bytesLeft -= bytesSent;
	}
	if(bytesSent < 0) {
		fprintf(stderr, "Failed to send request! \n");  // REMOVE: DEBUG_INFO
        // Cleanup: what should be the status when request sending fail?
		return -1;
	} 
	fprintf(stdout, "Request %s, Request length %d, Bytes sent %d\n", ReqStr, requestLen, totalSent);  // REMOVE: DEBUG_INFO
	//shutdown(sd, 1);  // disable future sends.

	// 3. Receive a response header until completion, up to [sizeof(HdrStr) - 1] bytes
	char HdrStr[1024];  // To hold header contents
	memset(HdrStr, 0, sizeof(HdrStr));
	int headerLen = 0;
    int bytesRecv = 0;
	char * MarkerPtr;  
	while ((bytesRecv = recv(sd, HdrStr + headerLen, sizeof(HdrStr) - 1 - headerLen, 0)) > 0) {
			fprintf(stdout, " Received %d header bytes\n", bytesRecv);  // REMOVE: DEBUG_INFO
			headerLen += bytesRecv;
			HdrStr[headerLen] = '\0';  // Add '\0' temporily to ensure HdrStr is null-terminated, the next receive will overwrite this byte, or this will be the last receive.
			if((MarkerPtr = strstr(HdrStr, "\r\n\r\n")) != NULL) {  // MarkerPtr will point to the start of the end marker in HdrStr
				break;
			}  
	}
	if (bytesRecv <= 0) {
		gfr->status = GF_INVALID;
		return -1;
	}
    
	// Post-process
	char RestoreBuf[1024];  // For storing the very beginning portion of of the file contents
	memset(RestoreBuf, 0 ,sizeof(RestoreBuf));
	char *contentHead = MarkerPtr + 4;
	size_t bytesHeaderPlusMarker = (size_t)(contentHead - HdrStr);
	size_t bytesToRestore = headerLen - bytesHeaderPlusMarker;
	memcpy(RestoreBuf, contentHead, bytesToRestore);  // copy contents from after end marker
    //*MarkerPtr = '\0';  // This will end HdrStr as a C-string.
	//fprintf(stdout, " Final Header string is: %s\n", HdrStr);  // REMOVE: DEBUG_INFO

    char *headerPtr = HdrStr;
    char *scheme;
	char *status;
	char *length;
	scheme = strsep(&headerPtr, " ");
	status = strsep(&headerPtr, " \r\n\r\n");
	length = strsep(&headerPtr, "\r\n\r\n");  // with trailing space or not
	fprintf(stdout, " scheme = %s, status = %s, length = %s\n", scheme, status, length);  // REMOVE: DEBUG_INFO

	if (scheme == NULL || status == NULL || length == NULL || strcmp(scheme, "GETFILE") != 0) {
		fprintf(stdout, " Invalid header format! \n");
		gfr->status = GF_INVALID;
		return -1;
	}
	else if (strcmp(status, "OK") == 0)
	{
		fprintf(stdout, " Status - OK \n");  // REMOVE: DEBUG_INFO
		gfr->status = GF_OK;
	}
	else if (strcmp(status, "INVALID") == 0)
	{
		fprintf(stdout, " Status - INVALID \n");  // REMOVE: DEBUG_INFO
		gfr->status = GF_INVALID;
		return -1;
	}
	else if (strcmp(status, "FILE_NOT_FOUND") == 0)
	{
		fprintf(stdout, " Status - FILE_NOT_FOUND \n");  // REMOVE: DEBUG_INFO
		gfr->status = GF_FILE_NOT_FOUND;
	}
	else if (strcmp(status, "ERROR") == 0)
	{
		fprintf(stdout, " Status - ERROR \n");  // REMOVE: DEBUG_INFO
		gfr->status = GF_ERROR;
	}
	else {
		fprintf(stdout, " Status - UNKNOWN \n");  // REMOVE: DEBUG_INFO
		gfr->status = GF_INVALID;
		return -1;
	}

	if (gfr->status == GF_OK) { 
	    if (length[strlen(length) - 1] == ' ') {
		    length[strlen(length) - 1] = '\0';  // Get rid of possible trailing space
		}
		gfr->fileLength = atoi(length);
	}  
	
	fprintf(stdout, " File Length is %zu bytes \n", gfr->fileLength);  // REMOVE: DEBUG_INFO

	callback proc = gfr->headerfunc;
	if (proc) {
		(*proc)(HdrStr, headerLen, gfr->headerarg);  // Invoke the header callback 
	}
     
    // 5. Receive file contents from server until completion.
    // Restore the first chunk arraived along with the header bytes.
	fprintf(stdout, " total content bytes is %zu bytes \n", bytesToRestore);
	proc = gfr->writefunc;
    if(proc) {
		(*proc)(RestoreBuf, bytesToRestore, gfr->writearg);  // strlen will exclude the trailing '\0' we added to RestoreBuf
	}

    int totalBytes = bytesToRestore;
    char recvChunkBuf[RECV_CHUNK_SIZE];
    memset(recvChunkBuf, 0, sizeof(recvChunkBuf));
    bytesRecv = 0;
	if (proc && (gfr->status == GF_OK) && (bytesToRestore < gfr->fileLength) ) {
		while ((bytesRecv = recv(sd, recvChunkBuf, sizeof(recvChunkBuf), 0)) > 0) {
			(*proc)(recvChunkBuf, bytesRecv, gfr->writearg);  //Invoke the callback
			totalBytes += bytesRecv;
			fprintf(stdout, " Received %d bytes, total %d bytes\n", bytesRecv, totalBytes);  // REMOVE: DEBUG_INFO
			memset(recvChunkBuf, 0, sizeof(recvChunkBuf));
			if(totalBytes >= gfr->fileLength) { 
				break; 
			}
		}
		if (bytesRecv <= 0) {
			fprintf(stdout, " Error transferring contents!\n");  // REMOVE: DEBUG_INFO
			return -1;
		}
	}
	gfr->bytesRecv = totalBytes;
    close(sd);

    return 0;
}

void gfc_set_headerarg(gfcrequest_t *gfr, void *headerarg){
	gfr->headerarg = headerarg;
}

void gfc_set_headerfunc(gfcrequest_t *gfr, void (*headerfunc)(void*, size_t, void *)){
	gfr->headerfunc = headerfunc;
}

void gfc_set_path(gfcrequest_t *gfr, char* path){
    strcpy(gfr->path, path);
	//gfr->path = path;
}

void gfc_set_port(gfcrequest_t *gfr, unsigned short port){
	gfr->port = port;
}

void gfc_set_server(gfcrequest_t *gfr, char* server){
    strcpy(gfr->server, server);
}

void gfc_set_writearg(gfcrequest_t *gfr, void *writearg){
	gfr->writearg = writearg;
}

void gfc_set_writefunc(gfcrequest_t *gfr, void (*writefunc)(void*, size_t, void *)){
	gfr->writefunc = writefunc;
}

const char* gfc_strstatus(gfstatus_t status){
    const char *strstatus = "UNKNOWN";

    switch (status)
    {
        default:
        break;

        case GF_OK: {
            strstatus = "OK";
        }
        break;
        
        case GF_ERROR: {
            strstatus = "ERROR";
        }
        break;

        case GF_FILE_NOT_FOUND: {
            strstatus = "FILE_NOT_FOUND";
        }
        break;

        case GF_INVALID: {
            strstatus = "INVALID";
        }
        break;
    }

    return strstatus;
}

bool parseHdr(char *HdrStr, gfcrequest_t *gfr)
{
	bool rv = false;

    // "GETFILE status length \r\n\r\n" Cleanup: how if header not null terminated?
	char* tokens[4];  // to store tokens extracted from header
	int index = 0;
	char* token = strtok(HdrStr, " ");
	while (token) {
		fprintf(stdout, "Header token %d: %s\n", index, token);  // DEBUG_PRINT
		tokens[index++] = token;
		token = strtok(NULL, " ");
	}

	// Second token contains status
	if (strcmp(tokens[1], "OK") == 0)
	{
		fprintf(stdout, " Status - OK \n");  // REMOVE: DEBUG_INFO
		rv = true;
		gfr->status = GF_OK;
	}
	else if (strcmp(tokens[1], "INVALID") == 0)
	{
		fprintf(stdout, " Status - INVALID \n");  // REMOVE: DEBUG_INFO
		gfr->status = GF_INVALID;
	}
	else if (strcmp(tokens[1], "FILE_NOT_FOUND") == 0)
	{
		fprintf(stdout, " Status - FILE_NOT_FOUND \n");  // REMOVE: DEBUG_INFO
		gfr->status = GF_FILE_NOT_FOUND;
	}
	else if (strcmp(tokens[1], "ERROR") == 0)
	{
		fprintf(stdout, " Status - ERROR \n");  // REMOVE: DEBUG_INFO
		gfr->status = GF_ERROR;
	}
	else {  // should never reach here
		fprintf(stdout, " Status - UNKNOWN \n");  // REMOVE: DEBUG_INFO
	}

	// Third token contains file length
	gfr->fileLength = atoi(tokens[2]);
	fprintf(stdout, " File Length is %s bytes \n", tokens[2]);  // REMOVE: DEBUG_INFO
	
	return rv;
}
