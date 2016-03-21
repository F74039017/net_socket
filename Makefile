all : transfer

transfer : transfer.cpp
	g++ transfer.cpp -pthread -o transfer

debug : transfer.cpp
	g++ -D DEBUG transfer.cpp -pthread -o transfer

rto : transfer.cpp
	g++ -D RTO transfer.cpp -pthread -o transfer

clear :
	rm transfer
