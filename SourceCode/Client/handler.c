#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

#include "gfserver.h"
#include "gfserver-student.h"
#include "content.h"
#include <pthread.h>
#include "steque.h"

#define BUFFER_SIZE 12041
#define MIN(a, b) ((a < b) ? a : b)


// Global variables
steque_t requestQueue;
steque_t threadPool;
pthread_mutex_t mutex_rq = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t rq_nonEmpty = PTHREAD_COND_INITIALIZER;

// Defines eveything a worker thread should know to process a connection
typedef struct request { 
	gfcontext_t *ctx;    /// context passed in (opaque, can be view as equal to socket descriptor for this connection)
	gfstatus_t status;   /// status - used by worker thread to transfer header
	size_t fileLen;      /// file length - used by worker thread to transfer header
    const char * path;   /// file path - used by worker thread to parse the request string
	void* arg;           /// Additional arg that user passed in.
} request;


size_t getFileLength(int fd) {
	if(fd < 0) {
		return 0;  // invalid file descriptor, return 0 size.
	}
	struct stat st;
    fstat(fd, &st);  // Must use fstat() to be thread safe
	
	return st.st_size;
}  

//
//  The purpose of this function is to handle a get request
//
//  The ctx is the "context" operation and it contains connection state
//  The path is the path being retrieved
//  The arg allows the registration of context that is passed into this routine.
//  Note: you don't need to use arg. The test code uses it in some cases, but
//        not in others.
//
ssize_t gfs_handler(gfcontext_t *ctx, const char *path, void* arg){	
	request *req = (request *)malloc(sizeof(request));  // must allocate request on the heap, so that worker threads can get to it.
	req->ctx = ctx;
	req->path = path;

	// Enqueue the transfer request.
	pthread_mutex_lock(&mutex_rq);
	steque_enqueue(&requestQueue, (steque_item)req);
	pthread_mutex_unlock(&mutex_rq);
	pthread_cond_broadcast(&rq_nonEmpty);  // Signal all the worker threads

	return 0;
}


// Worker thread which handles file transfer requests
void* transferHandler(void* arg) {
	// Loops forever, each loop cycle handles a file transfer request
	while (1) { 
		request *req = NULL;

		// Get a request from the queue
		pthread_mutex_lock(&mutex_rq);
		while (steque_isempty(&requestQueue)) {  // must use while loop since multiple threads competes to get the mutex
			pthread_cond_wait(&rq_nonEmpty, &mutex_rq);
		}
		req = (request *)steque_pop(&requestQueue);
		pthread_mutex_unlock(&mutex_rq);

        if(req) {
            int fd = content_get(req->path);
	        req->status = (fd == -1) ? GF_FILE_NOT_FOUND : GF_OK;
	        req->fileLen = getFileLength(fd);

            // Always send the header, fileLen will be 0 if file not found
		    gfs_sendheader(req->ctx, req->status, req->fileLen);  

		    // Send the file if OK
			if (req->status == GF_OK && fd > 0) {  
			    char sendBuff[BUFFER_SIZE];
				int bytesSent = 0;
				int totalSent = 0;
			    while (totalSent < req->fileLen) {
				    memset(sendBuff, 0, sizeof(sendBuff));
				    pread(fd, sendBuff, sizeof(sendBuff), totalSent);  // Must use pread() to be thread safe
				    bytesSent = gfs_send(req->ctx, sendBuff, MIN(sizeof(sendBuff), (req->fileLen - totalSent)));
				    totalSent += bytesSent;
			    }
		    }
		
		    // Clean up the request memory allocated by the boss thread
		    free(req);
		    req = NULL;
		}
	}
}

// Create worker thread pool with passed in number of threads.
void createWorkerThreads(int nthreads) {
	steque_init(&threadPool);

	for (int i = 0; i < nthreads; i++) {
		pthread_t *tid = (pthread_t *)malloc(sizeof(pthread_t));  // must be freed later 
		pthread_create(tid, NULL, &transferHandler, NULL); 
		pthread_detach(*tid);
		steque_enqueue(&threadPool, (steque_item)tid);
	}
}

// Initialze a request queue
void initRequestQueue() {
	steque_init(&requestQueue);
}

