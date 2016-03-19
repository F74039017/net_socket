all : transfer

server : server.cpp
	gcc server.cpp -pthread -o server

client : client.cpp
	gcc client.cpp -pthread -o client

transfer : transfer.cpp
	gcc transfer.cpp -pthread -o transfer

clean :
	rm server client *.o
