# Multi-Threaded File Transfer Server and Client - Socket Programming
Designed and implemented a multi-threaded web server that serves static files based on a GetFile protocol. Alongside the server, designed and created a multi-threaded client that acts as a load generator for the server. Both the server and client are written in C and are based on a sound, scalable design. Socket programming libraries and PThread library (for multi-threading) are used. <br />

Getfile is a simple protocol used to transfer a file from one computer to another that I made up for this project. A typical successful transfer is illustrated below. 

![alt text](https://github.com/JulianMei/Socket-programming-Multi-Threaded-File-Transfer-Server-and-Client/blob/master/gftransfer.png)  <br />

Based on the protocol, designed and implemented the server and client following a boss-worker thread pattern. The design supports concurrent file transfer from server machine to client machine through socket communication. <br /><br />
High level design and code structure is described in the following diagram: 

![alt text](https://github.com/JulianMei/Multi-threaded_File_Transfer_Server_Client_Socket_Programming/blob/master/High_Level_Design.jpg)

