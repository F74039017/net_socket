all : transfer

server : server.cpp
	g++ server.cpp -pthread -o server

client : client.cpp
	g++ client.cpp -pthread -o client

transfer : transfer.cpp
	g++ transfer.cpp -pthread -o transfer

clean :
	rm server client *.o
