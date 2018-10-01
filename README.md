# Implements a Multi-Threaded File Transfer Server Client Pair using C socket programming libraries.
Foreword
In this project, I designed and implemented a multi-threaded web server that serves static files based on a GetFile protocol. Alongside the server, I also create a multi-threaded client that acts as a load generator for the server. Both the server and client are written in C and are based on a sound, scalable design.

Part 1: Implementing the Getfile Protocol
Getfile is a simple protocol used to transfer a file from one computer to another that I made up for this project. A typical successful transfer is illustrated below.

Getfile Transfer Figure

The general form of a request is: <scheme> <method> <path>\r\n\r\n
Note:
The scheme is always GETFILE.
The method is always GET.
The path must always start with a ‘/’.
  
The general form of a response is: <scheme> <status> <length>\r\n\r\n<content>
Note:
The scheme is always GETFILE.
The status must be in the set {‘OK’, ‘FILE_NOT_FOUND’, ‘ERROR’, 'INVALID'}.
INVALID is the appropriate status when the header is invalid. This includes a malformed header as well an incomplete header due to communication issues.
FILE_NOT_FOUND is the appropriate response whenever the client has made an error in his request. ERROR is reserved for when the server is responsible for something bad happening.
No content may be sent if the status is FILE_NOT_FOUND or ERROR.
When the status is OK, the length should be a number expressed in ASCII (what sprintf will give you). The length parameter should be omitted for statuses of FILE_NOT_FOUND or ERROR.
The sequence ‘\r\n\r\n’ marks the end of the header. All remaining bytes are the files contents.
The space between the <scheme> and the <method> and the space between the <method> and the <path> are required. The space between the <path> and '\r\n\r\n' is optional.
The space between the <scheme> and the <status> and the space between the <status> and the <length> are required. The space between the <length> and '\r\n\r\n' is optional.


Part 2: Implementing a Multithreaded Getfile Server

In Part 1, the Getfile server can only handle a single request at a time. Similarly on the client side, the Getfile workload generator can only download a single file at a time. To overcome this limitation, I made the Getfile server and client multi-threaded by following a boss-worker thread pattern. 
