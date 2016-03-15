#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h> // socket
#include <arpa/inet.h> //inet_addr
#include <unistd.h>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>

/* buffer length */
#define MAX_FILENAME_LEN 100
#define MAX_COMMAND_LEN 50
#define MAX_MESSAGE_LEN 1024
#define MAX_FLAG_LEN 50

#define THREAD_FAIL 1

#define NONE 0
#define CMD 1
#define RECV 2

bool state = NONE;
void* TCP_command_handler(void *);
void* TCP_recv_handler(void *);
int sendall(int sock, char* buf, int len);
int main(int argc , char *argv[])
{
    int sock_cmd_desc, sock_recv_desc;
    char ip[20];
    int port;
    struct sockaddr_in server;

    if(argc == 1)
    {
        puts("Use default ip: localhost, port: 8888");
		strcpy(ip, "127.0.0.1");
        port = 8888;
    }
    else
    {
        if(!strcmp(argv[1], "localhost"))
			strcpy(ip, "127.0.0.1");
        else
			strcpy(ip, argv[1]);

        if(argc==2)
        {
            puts("Use default port: 8888");
            port = 8888;
        }
        else
            port = atoi(argv[2]);
    }
     
    // Create sockets
    sock_cmd_desc = socket(AF_INET , SOCK_STREAM , 0);
    sock_recv_desc = socket(AF_INET , SOCK_STREAM , 0);
    if (sock_cmd_desc==-1 || sock_recv_desc==-1)
    {
        printf("Could not create socket");
    }
         
    server.sin_addr.s_addr = inet_addr( ip );
    server.sin_family = AF_INET;
    server.sin_port = htons( port );

 
    // Connet command socket
    if (connect(sock_cmd_desc , (struct sockaddr *)&server , sizeof(server)) < 0)
    {
        puts("connect error");
        return 1;
    }
    puts("Connected cmd");

    pthread_t sock_cmd_thread;
    int* param_desc = (int*)malloc(sizeof(int));
    *param_desc = sock_cmd_desc;
    if(pthread_create( &sock_cmd_thread, NULL, TCP_command_handler, (void*)param_desc) < 0)
    {
        perror("Fail to create socket pthread");
        return THREAD_FAIL;
    }

	// Connect recv socket
    if (connect(sock_recv_desc, (struct sockaddr *)&server , sizeof(server)) < 0)
    {
        puts("connect error");
        return 1;
    }
    puts("Connected recv");

    pthread_t sock_recv_thread;
    param_desc = (int*)malloc(sizeof(int));
    *param_desc = sock_recv_desc;
    if(pthread_create( &sock_recv_thread, NULL, TCP_recv_handler, (void*)param_desc) < 0)
    {
        perror("Fail to create socket pthread");
        return THREAD_FAIL;
    }

    pthread_join(sock_cmd_thread, NULL);
    pthread_join(sock_recv_desc, NULL);
    close(sock_cmd_desc);
    return 0;
}

void* TCP_command_handler(void* socket_desc)
{
    int sock = *(int*)socket_desc;
    int write_size;
	char command[MAX_COMMAND_LEN];
    char message[MAX_MESSAGE_LEN+1];
	char filename[MAX_FILENAME_LEN];
	char transferFlag[MAX_FLAG_LEN];
	size_t read_size;
	long long filesize;

	while(1)
	{
		fgets(message, MAX_MESSAGE_LEN-1, stdin);
		if(state == RECV)
		{
			puts("Receiving data, please wait");
			continue;
		}
		message[strlen(message)-1] = '\0';
		if(command[0]=='\n')
			continue;
		sendall(sock, message, strlen(message));
		sscanf(message, "%s %s", command, filename);

		/* commands */
		if(!strcmp(command, "get"))  // get command
		{
			if((read_size = recv(sock, message, MAX_MESSAGE_LEN-1, 0)) > 0) // wait pn or FILE_NOT_FOUND flag
			{
				message[strlen(message)] = '\0';
				if(!strcmp(message, "FILE_NOT_FOUND")) // if file doesn't exist => next command
					printf("%s\n", "FILE_NOT_FOUND\n");
				else
				{
					FILE* fp = fopen(filename, "wb");
					message[strlen(message)] = '\0';
					long long pn = atoll(message); // record expected packet numbers
					if(pn==0)
						continue;

					/* send READY_RECV flag */
					strcpy(transferFlag, "READY_RECV");
					sendall(sock, transferFlag, strlen(transferFlag));

					/* start receive data from server */
					long long pcnt = 0;
					time_t ts, lts=0;
					FILE* lfp = fopen("log.txt", "a");
					fprintf(lfp, "\tget %s\n", filename);
					printf("\tget %s\n", filename);
					while((read_size = recv(sock, message, MAX_MESSAGE_LEN-1, 0)) > 0)
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
							sendall(sock, transferFlag, strlen(transferFlag));
						}
						else
						{
							strcpy(transferFlag, "COMPLETE");
							sendall(sock, transferFlag, strlen(transferFlag));
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
		}
		else if(!strcmp(command, "put")) // put command
		{
			if((read_size = recv(sock, message, MAX_MESSAGE_LEN-1, 0)) <= 0)
				continue;

			/* Start transfer data */
			FILE* fp = fopen(filename, "rb");
			if(fp)
			{
				/* send number of packets */
				struct stat st;
				int result = stat(filename, &st);
				long long fsize = st.st_size; // filesize
				long long pn = fsize/MAX_MESSAGE_LEN;
				if(fsize%MAX_MESSAGE_LEN)
					pn++;
				sprintf(message, "%lld", pn);
				sendall(sock, message, strlen(message)); // send expected packet numbers

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
					if(read_size = fread(message, sizeof(uint8_t), MAX_MESSAGE_LEN-1, fp))
					{
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
			else
			{
				strcpy(message, "FILE_NOT_FOUND");
				sendall(sock, message, strlen(message));
			}

			fclose(fp);
			fp = NULL;
		}
		else // no such command
			continue;

		if(read_size == 0)
		{
			puts("Server disconnected");
			fflush(stdout);
			break;
		}
		else if(read_size == -1)
		{
			perror("recv failed");
			break;
		}
	}
	// Free the socket pointer
	free(socket_desc);
}

void *TCP_recv_handler(void *socket_desc)
{
    int sock = *(int*)socket_desc;
    uint32_t read_size;
    char *server_message , command[MAX_COMMAND_LEN], message[MAX_MESSAGE_LEN+1];
	char transferFlag[MAX_FLAG_LEN];
    char filename[MAX_FILENAME_LEN];

	while((read_size = recv(sock, message, MAX_MESSAGE_LEN-1, 0)) > 0)
	{
		state = RECV;
		message[read_size] = '\0';
		sscanf(message, "%s %s", command, filename);
		if(!strcmp(command, "get"))
		{
			/* Start transfer data */
			FILE* fp = fopen(filename, "rb");
			if(fp)
			{
				puts("start transfer");
				/* send number of packets */
				struct stat st;
				int result = stat(filename, &st);
				long long fsize = st.st_size; // filesize
				long long pn = fsize/MAX_MESSAGE_LEN;
				if(fsize%MAX_MESSAGE_LEN)
					pn++;
				sprintf(message, "%lld", pn);
				sendall(sock, message, strlen(message)); // send expected packet numbers

				/* wait READY_RECV or COMPLETE flag */
				long long pcnt = 0;
				while((read_size = recv(sock, message, MAX_MESSAGE_LEN-1, 0)) > 0) //++ Assume that ACK flag never lost
				{
					if(!strncmp(message, "COMPLETE", 8))
						break;
					
					/* start to transfer data */
					if(read_size = fread(message, sizeof(uint8_t), MAX_MESSAGE_LEN-1, fp))
					{
						sendall(sock, message, read_size);  // tranfer data to client until EOF
					}
				}
				puts("finish");
			}
			else
			{
				strcpy(message, "FILE_NOT_FOUND");
				sendall(sock, message, strlen(message));
			}

			fclose(fp);
			fp = NULL;
		}
		else if(!strcmp(command, "put"))
		{
			/* prevent recv command and wait flag at same time */
			strcpy(transferFlag, "PREPARE");
			sendall(sock, transferFlag, strlen(transferFlag));

			if((read_size = recv(sock, message, MAX_MESSAGE_LEN-1, 0)) > 0) // wait pn or FILE_NOT_FOUND flag
			{
				message[strlen(message)] = '\0';
				if(!strcmp(message, "FILE_NOT_FOUND")) // if file doesn't exist => next command
					printf("%s\n", "FILE_NOT_FOUND\n");
				else
				{
					puts("start download");
					FILE* fp = fopen(filename, "wb");
					message[strlen(message)] = '\0';
					long long pn = atoll(message); // record expected packet numbers

					/* send READY_RECV flag */
					strcpy(transferFlag, "READY_RECV");
					sendall(sock, transferFlag, strlen(transferFlag));

					/* start receive data from server */
					long long pcnt = 0;
					while((read_size = recv(sock, message, MAX_MESSAGE_LEN-1, 0)) > 0)
					{
						pcnt++;
						message[read_size] = '\0';
						/* DEBUG - OUTPUT DOWNLOAD DATA TO STDOUT */
						//printf("%s", message);
						//fflush(stdout);
						
						fwrite(message, sizeof(uint8_t), read_size, fp);
						fflush(fp);
	
						/* response client state */
						if(pcnt<pn)
						{
							strcpy(transferFlag, "READY_RECV");
							sendall(sock, transferFlag, strlen(transferFlag));
						}
						else
						{
							puts("send complete");
							strcpy(transferFlag, "COMPLETE");
							sendall(sock, transferFlag, strlen(transferFlag));
							break;
						}
					}
					puts("Complete");
					fflush(stdout);
				}
			}
		}
		else
		{
			puts("error: no such command");
		}
		state = NONE;
	}
}

int sendall(int sock, char* buf, int len)
{
	int total = 0;
	int bytesleft;
	bytesleft = strlen(buf);
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
