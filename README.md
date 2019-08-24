# Multi-Threaded File Transfer Server Client Socket Programming
Designed and implemented a multi-threaded web server that serves static files based on a GetFile protocol. Alongside the server, designed and created a multi-threaded client that acts as a load generator for the server. Both the server and client are written in C and are based on a sound, scalable design. 

Part 1: Implement the Getfile Protocol
Getfile is a simple protocol used to transfer a file from one computer to another that I made up for this project. A typical successful transfer is illustrated below.

![alt text](https://github.com/JulianMei/Socket-programming-Multi-Threaded-File-Transfer-Server-and-Client/blob/master/gftransfer.png)

Part 2: Implement a Multithreaded Getfile Server and Client
In Part 1, the Getfile server can only handle a single request at a time. Similarly on the client side, the Getfile workload generator can only download a single file at a time. To overcome this limitation, I made the Getfile server and client multi-threaded by following a boss-worker thread pattern. The high-level code design is illustrated below.

![alt text](https://github.com/JulianMei/Multi-threaded_File_Transfer_Server_Client_Socket_Programming/blob/master/High%20Level%20Design.jpg)

