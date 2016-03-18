#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h> // socket
#include <arpa/inet.h> //inet_addr
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>
#include <ctime>
#include <pthread.h>

/* Fail Code */
#define SERVER_SOCKET_FAIL 1
#define SERVER_BIND_FAIL 2
#define PTHREAD_CREATE_FAIL 3
#define ARG_ERROR 4
#define CLIENT_SOCKET_FAIL 5
#define CONNECT_FAIL 6
#define BIND_FAIL 7
#define FILE_NOT_FOUND 8

#define MAX_CLIENT 3

/* buffer length */
#define MAX_FILENAME_LEN 100
#define MAX_COMMAND_LEN 50
#define MAX_MESSAGE_LEN 1024
#define MAX_FLAG_LEN 50

/* TCP UDP protocal */
#define TCP 0
#define UDP 1

/* send or recv mode */
#define SEND 0
#define RECV 1

unsigned int addr_len = sizeof(struct sockaddr_in);

void *TCP_handler(void *);
void *UDP_handler(void *);
void intHandler(int sig);
int sendall(int sock, char* buf, int len);
int sendallto(int sock, char* buf, int len, struct sockaddr_in* addr_info);
int recvall(int sock, char* buf);
int recvallfrom(int sock, char* buf, struct sockaddr_in *addr_from);

char filename[MAX_FILENAME_LEN];
int protocal, mode, port;
char ip[20];

struct _UDP_info
{
	int sock;
	struct sockaddr_in addr_info;
};

int main(int argc, char** argv)
{
    signal(SIGINT, intHandler);

	/* Set tranfer mode */
	char ip[20];

	if(argc<5)
	{
		puts("Argument error: [send/recv] <ip> <port> [filename]");
		return ARG_ERROR;
	}
	else
	{
		/* Set protocal */
		if(!strcmp(argv[1], "tcp") && !strcmp(argv[1], "udp"))
		{
			puts("protocal error: [tcp / udp]");
			return ARG_ERROR;
		}
		protocal = !strcmp(argv[1], "tcp")? TCP: UDP;

		/* Set transfer mode */
		if(!strcmp(argv[2], "send") && !strcmp(argv[2], "recv"))
		{
			puts("mode error: [send / recv]");
			return ARG_ERROR;
		}
		mode = !strcmp(argv[2], "send")? SEND: RECV;

		/* Set Ip address */
        if(!strcmp(argv[3], "localhost")) //++ doesn't check format
			strcpy(ip, "127.0.0.1");
        else
			strcpy(ip, argv[3]);

		/* Set port */
		port = atoi(argv[4]); //++ doesn't check format

		/* Set filename */
		if(argc==6)
			strcpy(filename, argv[5]);
	}


    int ss_desc, cs_desc;
    struct sockaddr_in server , client;
    int* sock;
    // Create socket
	if(protocal==TCP)
	{
		if(mode==RECV)
			sock = &ss_desc;
		else
			sock = &cs_desc;

		*sock = socket(AF_INET , SOCK_STREAM , 0);	// IPv4, TCP
		if(*sock==-1)
		{
			fprintf(stderr, "Fail to create server's socket");
			return SERVER_SOCKET_FAIL;
		}
		// set reuse address => avoid TCP TIME_WAIT session
		/* Enable address reuse */
		int on = 1;
		setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) );
	}
	else
	{
		if(mode==RECV)
		{
			ss_desc = socket(AF_INET , SOCK_DGRAM, 0);	// IPv4, UDP
			if (ss_desc==-1)
			{
				fprintf(stderr, "Fail to create server's socket");
				return SERVER_SOCKET_FAIL;
			}
		}
		else
		{
			cs_desc = socket(AF_INET , SOCK_DGRAM, 0);	// IPv4, UDP
			if (cs_desc==-1)
			{
				fprintf(stderr, "Fail to create server's socket");
				return SERVER_SOCKET_FAIL;
			}

			client.sin_family = AF_INET;
			client.sin_addr.s_addr = INADDR_ANY;
			client.sin_port = htons(0); // use any available port
		}
	}

    // Server socket info. sockaddr_in struct
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons( port );
     

	pthread_t thread;
	int *param_desc;
	if(protocal== TCP)
	{
		if(mode==SEND) // client
		{
			// Connet command socket
			if (connect(cs_desc, (struct sockaddr *)&server , sizeof(server)) < 0)
			{
				puts("connect error");
				return 1;
			}
			puts("Connected cmd");
			// Create pthread for recv
			param_desc = (int *)malloc(sizeof(int));
			*param_desc = cs_desc;
			 
			if( pthread_create( &thread, NULL ,  TCP_handler, (void*) param_desc) < 0)
			{
				perror("Fail to create new pthread");
				return PTHREAD_CREATE_FAIL;
			}
		}
		else // server
		{
			// Bind
			if(bind(ss_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
			{
				puts("bind failed");
				return SERVER_BIND_FAIL;
			}
			puts("bind done");

			// Listen. Accept at most MAX_CLIENT clients
			listen(ss_desc, MAX_CLIENT);
			 
			// accept() to wait for new clients
			puts("Waiting for incoming connections...");
			int structlen = sizeof(struct sockaddr_in);
			if( (cs_desc = accept(ss_desc, (struct sockaddr *)&client, (socklen_t*)&structlen)) ) // only accpet one client
			{
				// Create pthread for recv
				param_desc = (int *)malloc(sizeof(int));
				*param_desc = cs_desc;
				 
				if( pthread_create( &thread, NULL ,  TCP_handler, (void*) param_desc) < 0)
				{
					perror("Fail to create new pthread");
					return PTHREAD_CREATE_FAIL;
				}
			}

			if (cs_desc<0)
			{
				perror("Accept recv	failed");
				return -1;
			}
		}
	}
	else // UDP process
	{
		if(mode == SEND)
		{
			// Bind
			if(bind(cs_desc, (struct sockaddr *)&client, sizeof(client)) < 0)
			{
				puts("bind failed");
				return SERVER_BIND_FAIL;
			}
			puts("bind done");

			// Create pthread for recv
			struct _UDP_info* UDP_info = (struct _UDP_info*)malloc(sizeof(struct _UDP_info));
			UDP_info->sock = cs_desc;
			UDP_info->addr_info = server; // target server info
			 
			if( pthread_create( &thread, NULL ,  UDP_handler, (void*) UDP_info) < 0)
			{
				perror("Fail to create new pthread");
				return PTHREAD_CREATE_FAIL;
			}
		}	
		else
		{
			// Bind
			if(bind(ss_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
			{
				puts("bind failed");
				return SERVER_BIND_FAIL;
			}
			puts("bind done");

			// Create pthread for recv
			struct _UDP_info* UDP_info = (struct _UDP_info*)malloc(sizeof(struct _UDP_info));
			UDP_info->sock = ss_desc;
			UDP_info->addr_info = server; // self addr info
			 
			if( pthread_create( &thread, NULL ,  UDP_handler, (void*) UDP_info) < 0)
			{
				perror("Fail to create new pthread");
				return PTHREAD_CREATE_FAIL;
			}
		}
	}
	pthread_join(thread, NULL);
	return 0;
}

void* TCP_handler(void* socket_desc)
{
    int sock = *(int*)socket_desc;
	char command[MAX_COMMAND_LEN];
    char message[MAX_MESSAGE_LEN+1];
	char transferFlag[MAX_FLAG_LEN];
	size_t read_size;
	long long filesize;

	/* commands */
	if(mode==RECV)
	{
		/* get filename */
		if((read_size = recv(sock, message, MAX_MESSAGE_LEN-1, 0)) > 0) 
			strncpy(filename, message, read_size);
		else
			puts("filename error");
		strcpy(transferFlag, "FILENAME_ACK");
		send(sock, transferFlag, strlen(transferFlag), 0); // filename ack 
		
		/* get data */
		if((read_size = recv(sock, message, MAX_MESSAGE_LEN-1, 0)) > 0) // wait pn 
		{
			FILE* fp = fopen(filename, "wb");
			message[strlen(message)] = '\0';
			long long pn = atoll(message); // record expected packet numbers

			/* send READY_RECV flag */
			strcpy(transferFlag, "READY_RECV");
			write(sock, transferFlag, strlen(transferFlag));

			/* start receive data from server */
			long long pcnt = 0;
			time_t ts, lts=0;
			FILE* lfp = fopen("log.txt", "a");
			fprintf(lfp, "\trecv %s\n", filename);
			printf("\trecv %s\n", filename);
			while((read_size = recvall(sock, message)) > 0)
			{
				pcnt++;
				message[read_size] = '\0';
				/* DEBUG - OUTPUT DOWNLOAD DATA TO STDOUT */
				//printf("%s", message);
				//fflush(stdout);
				
				fwrite(message, sizeof(uint8_t), read_size, fp);
				fflush(fp);
				
				/* create timestamp and log */
				time(&ts);
				if(lts<ts)
				{
					printf("%.1f%%\t%s", 1.0*pcnt/pn*100, ctime(&ts));
					fprintf(lfp, "%.1f%%\t%s", 1.0*pcnt/pn*100, ctime(&ts));
				}
				lts= ts;
				/* response client state */
				if(pcnt<pn)
				{
					strcpy(transferFlag, "READY_RECV");
					write(sock, transferFlag, strlen(transferFlag));
				}
				else
				{
					strcpy(transferFlag, "COMPLETE");
					write(sock, transferFlag, strlen(transferFlag));
					break;
				}
			}
			fprintf(lfp, "\n");
			fclose(lfp);
			lfp = NULL;

			puts("Complete");
			fflush(stdout);
		}
	}
	else if(mode == SEND)
	{
		/* Start transfer data */
		FILE* fp = fopen(filename, "rb");
		if(fp)
		{
			/* send filename to client */
			send(sock, filename, strlen(filename), 0); // send filename 
			if((read_size = recv(sock, message, MAX_MESSAGE_LEN-1, 0)) > 0) // wait filename ACK
			{
				/* send number of packets */
				struct stat st;
				int result = stat(filename, &st);
				long long fsize = st.st_size; // filesize
				long long pn = fsize/MAX_MESSAGE_LEN;
				if(fsize%MAX_MESSAGE_LEN)
					pn++;
				sprintf(message, "%lld", pn);
				write(sock, message, strlen(message)); // send expected packet numbers

				/* wait READY_RECV or COMPLETE flag */
				long long pcnt = 0;
				FILE* lfp = fopen("log.txt", "a");
				fprintf(lfp, "\tput %s\n", filename);
				printf("\tput %s\n", filename);
				time_t ts, lts=0;
				while((read_size = recv(sock, message, MAX_MESSAGE_LEN-1, 0)) > 0) //++ Assume that ACK flag never lost
				{
					if(!strncmp(message, "COMPLETE", 8))
						break;
					
					/* start to transfer data */
					uint32_t conv;
					if(read_size = fread(message, sizeof(uint8_t), MAX_MESSAGE_LEN, fp))
					{
						conv = htonl(read_size+4); // include header size
						send(sock, &conv, sizeof(uint32_t), 0);
						sendall(sock, message, read_size);  // tranfer data to client until EOF
						pcnt++;
					}

					/* log */
					time(&ts);
					if(lts<ts)
					{
						printf("%.1f%%\t%s", 1.0*pcnt/pn*100, ctime(&ts));
						fprintf(lfp, "%.1f%%\t%s", 1.0*pcnt/pn*100, ctime(&ts));
					}
					lts= ts;
				}
				puts("finish");
			}
		}
		else
		{
			puts("File not found!");
			exit(EXIT_FAILURE);
		}

		fclose(fp);
		fp = NULL;
	}

	if(read_size == 0)
	{
		puts("disconnected");
		fflush(stdout);
	}
	else if(read_size == -1)
	{
		perror("recv failed");
	}
	// Free the socket pointer
	free(socket_desc);
}

void *UDP_handler(void *param)
{
    struct _UDP_info UDP_info = *(struct _UDP_info*)param;
	int sock = UDP_info.sock;
	struct sockaddr_in addr_info = UDP_info.addr_info;
	struct sockaddr_in addr_from;

	char command[MAX_COMMAND_LEN];
    char message[MAX_MESSAGE_LEN+1];
	char transferFlag[MAX_FLAG_LEN];
	size_t read_size;
	long long filesize;

	/* commands */
	if(mode==RECV)
	{
		/* get filename */
		if((read_size = recvfrom(sock, message, MAX_MESSAGE_LEN-1, 0, (struct sockaddr*)&addr_from, &addr_len)) > 0) 
			strncpy(filename, message, read_size);
		else
			puts("filename error");
		printf("get filename %s\n", filename); // debug
		strcpy(transferFlag, "FILENAME_ACK");
		sendto(sock, transferFlag, strlen(transferFlag), 0, (struct sockaddr*)&addr_from, addr_len); // filename ack 
		
		/* get data */
		if((read_size = recvfrom(sock, message, MAX_MESSAGE_LEN-1, 0, (struct sockaddr*)&addr_from, &addr_len)) > 0) 
		{
			FILE* fp = fopen(filename, "wb");
			message[strlen(message)] = '\0';
			long long pn = atoll(message); // record expected packet numbers

			/* send READY_RECV flag */
			strcpy(transferFlag, "READY_RECV");
			sendto(sock, transferFlag, strlen(transferFlag), 0, (struct sockaddr*)&addr_from, addr_len); // ready flag

			/* start receive data from server */
			long long pcnt = 0;
			time_t ts, lts=0;
			FILE* lfp = fopen("log.txt", "a");
			fprintf(lfp, "\trecv %s\n", filename);
			printf("\trecv %s\n", filename);
			while((read_size = recvallfrom(sock, message, &addr_from)) > 0)
			{
				pcnt++;
				message[read_size] = '\0';
				/* DEBUG - OUTPUT DOWNLOAD DATA TO STDOUT */
				//printf("%s", message);
				//fflush(stdout);
				
				fwrite(message, sizeof(uint8_t), read_size, fp);
				fflush(fp);
				
				/* create timestamp and log */
				time(&ts);
				if(lts<ts)
				{
					printf("%.1f%%\t%s", 1.0*pcnt/pn*100, ctime(&ts));
					fprintf(lfp, "%.1f%%\t%s", 1.0*pcnt/pn*100, ctime(&ts));
				}
				lts= ts;
				/* response client state */
				if(pcnt<pn)
				{
					strcpy(transferFlag, "READY_RECV");
					sendto(sock, transferFlag, strlen(transferFlag), 0, (struct sockaddr*)&addr_from, addr_len);
				}
				else
				{
					strcpy(transferFlag, "COMPLETE");
					sendto(sock, transferFlag, strlen(transferFlag), 0, (struct sockaddr*)&addr_from, addr_len);
					break;
				}
			}
			fprintf(lfp, "\n");
			fclose(lfp);
			lfp = NULL;

			puts("Complete");
			fflush(stdout);
		}
	}
	else if(mode == SEND)
	{
		/* Start transfer data */
		FILE* fp = fopen(filename, "rb");
		if(fp)
		{
			/* send filename to client */
			sendto(sock, filename, strlen(filename), 0, (struct sockaddr*)&addr_info, addr_len); // send filename 
			if((read_size = recvfrom(sock, message, MAX_MESSAGE_LEN-1, 0, (struct sockaddr*)&addr_from, &addr_len)) > 0)  // wait filename ack
			{
				/* send number of packets */
				struct stat st;
				int result = stat(filename, &st);
				long long fsize = st.st_size; // filesize
				long long pn = fsize/MAX_MESSAGE_LEN;
				if(fsize%MAX_MESSAGE_LEN)
					pn++;
				sprintf(message, "%lld", pn);
				sendto(sock, message, strlen(message), 0, (struct sockaddr*)&addr_info, addr_len); // send expected packet numbers

				/* wait READY_RECV or COMPLETE flag */
				long long pcnt = 0;
				FILE* lfp = fopen("log.txt", "a");
				fprintf(lfp, "\tput %s\n", filename);
				printf("\tput %s\n", filename);
				time_t ts, lts=0;
				while((read_size = recvfrom(sock, message, MAX_MESSAGE_LEN-1, 0, (struct sockaddr*)&addr_from, &addr_len)) > 0) 
				{
					if(!strncmp(message, "COMPLETE", 8))
						break;
					
					/* start to transfer data */
					uint32_t conv;
					if(read_size = fread(message, sizeof(uint8_t), MAX_MESSAGE_LEN, fp))
					{
						conv = htonl(read_size+4); // include header size
						sendto(sock, &conv, sizeof(uint32_t), 0, (struct sockaddr*)&addr_from, addr_len);
						sendallto(sock, message, read_size, &addr_info);  // tranfer data to client until EOF
						pcnt++;
					}

					/* log */
					time(&ts);
					if(lts<ts)
					{
						printf("%.1f%%\t%s", 1.0*pcnt/pn*100, ctime(&ts));
						fprintf(lfp, "%.1f%%\t%s", 1.0*pcnt/pn*100, ctime(&ts));
					}
					lts= ts;
				}
				puts("finish");
			}
		}
		else
		{
			puts("File not found!");
			exit(EXIT_FAILURE);
		}

		fclose(fp);
		fp = NULL;
	}

	if(read_size == 0)
	{
		puts("disconnected");
		fflush(stdout);
	}
	else if(read_size == -1)
	{
		perror("recv failed");
	}
	// Free the socket pointer
	free(param);
	
}

/* remove trailing newline character */
void message_trim(char* msg)
{
    int len = strlen(msg);
    msg[len-1] = '\0';
}

void intHandler(int sig)
{
    exit(0);
}

int sendall(int sock, char* buf, int len)
{
	int total = 0;
	int bytesleft;
	bytesleft = len;
	int n;

	while(total<len)
	{
		n = send(sock, buf+total, bytesleft, 0);
		if(n==-1)
			break;
		total += n;
		bytesleft -= n;
	}

	return n==-1? -1: 0;
}

int sendallto(int sock, char* buf, int len, struct sockaddr_in* addr_info)
{
	int total = 0;
	int bytesleft;
	bytesleft = len;
	int n;

	while(total<len)
	{
		n = sendto(sock, buf+total, bytesleft, 0, (struct sockaddr*)addr_info, addr_len);
		if(n==-1)
			break;
		total += n;
		bytesleft -= n;
	}

	return n==-1? -1: 0;
}

/* save data in buf and return data size */
int recvall(int sock, char* buf)
{
	int recv_size = 0;
	uint32_t len = 0;
	char message[MAX_MESSAGE_LEN];
	char output[MAX_MESSAGE_LEN];
	int read_size;
	while((read_size = recv(sock, message, MAX_MESSAGE_LEN, 0)) > 0)
	{
		if(len==0 && read_size>=4)
			memcpy(&len, message, sizeof(uint32_t)), len=ntohl(len);

		memcpy(output+recv_size, message, read_size);
		recv_size += read_size;
		if(recv_size == len)
			break;
	}
	recv_size -= 4;
	memcpy(buf, output+4, recv_size);
	buf[recv_size] = 0;
	//printf("get message: %s\n", buf); // DEBUG
	return recv_size;
}

int recvallfrom(int sock, char* buf, struct sockaddr_in *addr_from)
{
	int recv_size = 0;
	uint32_t len = 0;
	char message[MAX_MESSAGE_LEN];
	char output[MAX_MESSAGE_LEN];
	int read_size;
	while((read_size = recvfrom(sock, message, MAX_MESSAGE_LEN, 0, (struct sockaddr*)addr_from, &addr_len)) > 0)
	{
		if(len==0 && read_size>=4)
			memcpy(&len, message, sizeof(uint32_t)), len=ntohl(len);

		memcpy(output+recv_size, message, read_size);
		recv_size += read_size;
		if(recv_size == len)
			break;
	}
	recv_size -= 4;
	memcpy(buf, output+4, recv_size);
	buf[recv_size] = 0;
	//printf("get message: %s\n", buf); // DEBUG
	return recv_size;
}
