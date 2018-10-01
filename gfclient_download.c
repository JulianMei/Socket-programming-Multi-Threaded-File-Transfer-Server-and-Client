#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netdb.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <getopt.h>
#include <sys/stat.h>

#include "gfclient.h"
#include "gfclient-student.h"
#include "workload.h"
#include "pthread.h"
#include "steque.h"

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  webclient [options]\n"                                                     \
"options:\n"                                                                  \
"  -h                  Show this help message\n"                              \
"  -n [num_requests]   Requests download per thread (Default: 4)\n"           \
"  -p [server_port]    Server port (Default: 12041)\n"                         \
"  -s [server_addr]    Server address (Default: 127.0.0.1)\n"                   \
"  -t [nthreads]       Number of threads (Default 32)\n"                       \
"  -w [workload_path]  Path to workload file (Default: workload.txt)\n"       \

/* Global variables ================================================== */
steque_t taskQueue;
steque_t threadPool;
pthread_mutex_t mutex_tq = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  tq_nonEmpty = PTHREAD_COND_INITIALIZER;

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
  {"help",          no_argument,            NULL,           'h'},
  {"nthreads",      required_argument,      NULL,           't'},
  {"nrequests",     required_argument,      NULL,           'n'},
  {"server",        required_argument,      NULL,           's'},
  {"port",          required_argument,      NULL,           'p'},
  {"workload-path", required_argument,      NULL,           'w'},
  {NULL,            0,                      NULL,             0}
};

static void Usage() {
	fprintf(stderr, "%s", USAGE);
}

static void localPath(char *req_path, char *local_path){
  static int counter = 0;

  sprintf(local_path, "%s-%06d", &req_path[1], counter++);
}

static FILE* openFile(char *path){
  char *cur, *prev;
  FILE *ans;

  /* Make the directory if it isn't there */
  prev = path;
  while(NULL != (cur = strchr(prev+1, '/'))){
    *cur = '\0';

    if (0 > mkdir(&path[0], S_IRWXU)){
      if (errno != EEXIST){
        perror("Unable to create directory");
        exit(EXIT_FAILURE);
      }
    }

    *cur = '/';
    prev = cur;
  }

  if( NULL == (ans = fopen(&path[0], "w"))){
    perror("Unable to open file");
    exit(EXIT_FAILURE);
  }

  return ans;
}

/* Callbacks ========================================================= */
static void writecb(void* data, size_t data_len, void *arg){
  FILE *file = (FILE*) arg;

  fwrite(data, 1, data_len, file);
}


/* Download request handler, worker threads starts execution from here */
void* getFileHandler(void* nrequests)
{
	gfcrequest_t* req = NULL;

	// Loop until this handler has processed [nrequests] requests
  int numReqHandled = 0;
	while (numReqHandled < (long)nrequests) {
		pthread_mutex_lock(&mutex_tq);

    // !!! Must have the while check here otherwise queue may underflow !!!
		while (steque_isempty(&taskQueue)) {
      pthread_cond_wait(&tq_nonEmpty, &mutex_tq);
    }

		req = (gfcrequest_t*)steque_pop(&taskQueue);  // Retrieve a task
		pthread_mutex_unlock(&mutex_tq);
    
    if(req) {  
      // Perform this download request 
		  gfc_perform(req);
		  gfc_cleanup(req);
      req = NULL;
      numReqHandled++;
    }
	}

	pthread_exit(NULL);  // Exit the current thread.
}

/* Create worker thread pool  =========================================*/
void createWorkerThreads(int nThreads, long nrequests) {
	steque_init(&threadPool);

	for (int i = 0; i < nThreads; i++) {
		pthread_t* tid = (pthread_t*)malloc(sizeof(pthread_t));  // must be freed later 
		pthread_create(tid, NULL, &getFileHandler, (void *)nrequests);  // should be joinable
		steque_enqueue(&threadPool, (steque_item)tid);
    fprintf(stdout, "Created thread %d \n", i);  // DEBUG_PRINT
	}
}

/* Join worker threads  ==============================================*/
void joinWorkerThreads() {
	while (!steque_isempty(&threadPool)) {
		pthread_t * tid = (pthread_t *)steque_pop(&threadPool);
		pthread_join(*tid, NULL);
    free(tid);  // must free pointer to this tid object
	}
}

/* Main ========================================================= */
int main(int argc, char **argv) {
/* COMMAND LINE OPTIONS ============================================= */
  char *server = "localhost";
  unsigned short port = 12041;
  char *workload_path = "workload.txt";

  int i = 0;
  int option_char = 0;
  long nrequests = 4;
  int nthreads = 32;
  //int returncode = 0;
  gfcrequest_t *gfr = NULL;
  FILE *file = NULL;
  char *req_path = NULL;
  char local_path[1033];

  setbuf(stdout, NULL); // disable caching

  // Parse and set command line arguments
  while ((option_char = getopt_long(argc, argv, "t:hn:xp:s:w:", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      case 'h': // help
        Usage();
        exit(0);
        break;                      
      case 'n': // nrequests
        nrequests = atoi(optarg);
        break;
      case 'p': // port
        port = atoi(optarg);
        break;
      default:
        Usage();
        exit(1);
      case 's': // server
        server = optarg;
        break;
      case 't': // nthreads
        nthreads = atoi(optarg);
        break;
      case 'w': // workload-path
        workload_path = optarg;
        break;
    }
  }

  if( EXIT_SUCCESS != workload_init(workload_path)){
    fprintf(stderr, "Unable to load workload file %s.\n", workload_path);
    exit(EXIT_FAILURE);
  }

  gfc_global_init();

  // Initialize task queue
  steque_init(&taskQueue);

  // Initialized worker thread pool
  createWorkerThreads(nthreads, nrequests);

  /*Making the requests...*/
  for(i = 0; i < nrequests * nthreads; i++){
    req_path = workload_get_path();

    if(strlen(req_path) > 500){
      fprintf(stderr, "Request path exceeded maximum of 500 characters\n.");
      exit(EXIT_FAILURE);
    }

    localPath(req_path, local_path);

    file = openFile(local_path);

    gfr = gfc_create();
    gfc_set_server(gfr, server);
    gfc_set_path(gfr, req_path);
    gfc_set_port(gfr, port);
    gfc_set_writefunc(gfr, writecb);
    gfc_set_writearg(gfr, file);

    fprintf(stdout, "Requesting %s%s\n", server, req_path);

	  // Enqueue the file download request to the task queue, it's up to the 
    // worker threads to process the requests and clean up the gfr memory.
	  pthread_mutex_lock(&mutex_tq);
	  steque_enqueue(&taskQueue, (steque_item)gfr);  // enqueue request
	  pthread_mutex_unlock(&mutex_tq);
    pthread_cond_broadcast(&tq_nonEmpty);  // signal all the workers
  }

  // wait for all the worker threads join before exit
  joinWorkerThreads();  

  gfc_global_cleanup();

  return 0;
}  

