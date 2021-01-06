CHATROOM
========

The program has two files:
server.c
client.c

Compile both of them using following command:
make Makefile all
Run the server application to a terminal specifying a port number, e.g.:
./server 3333
Run a number of the client application to other terminals using the same port number, e.g.:
./client 3333

Following the prompts, the client can register or login to the chatroom, join a number of groups, etc.
Information about users, contacts and groups are stored in the files users.txt and groups.txt.

!! IMPORTANT !!
Don't delete the files users.txt and groups.txt, or their original contents, as the chatroom depends on
them for running.
