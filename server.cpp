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

/* Fail Code */
#define SERVER_SOCKET_FAIL 1
#define SERVER_BIND_FAIL 2
#define PTHREAD_CREATE_FAIL 3

#define DEFAULT_PORT 8888
#define MAX_CLIENT 3

/* buffer length */
#define MAX_FILENAME_LEN 100
#define MAX_COMMAND_LEN 50
#define MAX_MESSAGE_LEN 1024
#define MAX_FLAG_LEN 50

void *TCP_recv_handler(void *);
void *TCP_command_handler(void *);
void *UDP_handler(void *);
void intHandler(int sig);

int main(int argc, char** argv)
{
    signal(SIGINT, intHandler);

	/* server's and client's file descriptor => -1 means error */
    int ss_desc , cs_desc;
    struct sockaddr_in server , client;
    char *message;
     
    // Create socket
    ss_desc = socket(AF_INET , SOCK_STREAM , 0);	// IPv4, TCP
    if (ss_desc == -1)
	{
        fprintf(stderr, "Fail to create server's socket");
		return SERVER_SOCKET_FAIL;
	}

    // set reuse address => avoid TCP TIME_WAIT session
    /* Enable address reuse */
    int on = 1;
    setsockopt( ss_desc, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) );

    /* Set port */
    int port = DEFAULT_PORT;
    if(argc==2)
        port = atoi(argv[1]);
    else
        printf("Use default port: %d\n", DEFAULT_PORT);

    // Server socket info. sockaddr_in struct
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons( port );
     
    // Bind
    if( bind(ss_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        puts("bind failed");
        return SERVER_BIND_FAIL;
    }
    puts("bind done");

    // Listen. Accept at most MAX_CLIENT clients
    listen(ss_desc , MAX_CLIENT);
     
    // accept() to wait for new clients
    puts("Waiting for incoming connections...");
    int structlen = sizeof(struct sockaddr_in);
    if( (cs_desc = accept(ss_desc, (struct sockaddr *)&client, (socklen_t*)&structlen)) )
    {
        //  DEBUG - CHECK CLIENT DESC
        // printf("Client %d connect!!\n", cs_desc);
         
		// Create pthread for recv
        pthread_t thread;
        int* param_desc = (int *)malloc(sizeof(int));
        *param_desc = cs_desc;
         
        if( pthread_create( &thread , NULL ,  TCP_recv_handler, (void*) param_desc) < 0)
        {
            perror("Fail to create new pthread");
            return PTHREAD_CREATE_FAIL;
        }
		pthread_join(thread, NULL);

		// Create pthread for command
		//if( (cs_desc = accept(ss_desc, (struct sockaddr *)&client, (socklen_t*)&structlen)) )
		//{
			//param_desc = (int *)malloc(sizeof(int));
			//*param_desc = cs_desc;
			 
			//if( pthread_create( &thread , NULL ,  TCP_command_handler, (void*) param_desc) < 0)
			//{
				//perror("Fail to create new pthread");
				//return PTHREAD_CREATE_FAIL;
			//}
			//pthread_join(thread, NULL);
		//}
    }

    if (cs_desc<0)
    {
        perror("Accept recv	failed");
        return 1;
    }
     
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
		message[strlen(message)-1] = '\0';
		if(command[0]=='\n')
			continue;
		write(sock, message, strlen(message));
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

					/* send READY_RECV flag */
					strcpy(transferFlag, "READY_RECV");
					write(sock, transferFlag, strlen(transferFlag));

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
					if(read_size = fread(message, sizeof(uint8_t), MAX_MESSAGE_LEN-1, fp))
					{
						write(sock, message, read_size);  // tranfer data to client until EOF
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
				write(sock, message, strlen(message));
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
				write(sock, message, strlen(message)); // send expected packet numbers

				/* wait READY_RECV or COMPLETE flag */
				long long pcnt = 0;
				while((read_size = recv(sock, message, MAX_MESSAGE_LEN-1, 0)) > 0) //++ Assume that ACK flag never lost
				{
					if(!strncmp(message, "COMPLETE", 8))
						break;
					
					/* start to transfer data */
					if(read_size = fread(message, sizeof(uint8_t), MAX_MESSAGE_LEN-1, fp))
					{
						write(sock, message, read_size);  // tranfer data to client until EOF
					}
				}
				puts("finish");
			}
			else
			{
				strcpy(message, "FILE_NOT_FOUND");
				write(sock, message, strlen(message));
			}

			fclose(fp);
			fp = NULL;
		}
		else if(!strcmp(command, "put"))
		{
			/* prevent recv command and wait flag at same time */
			strcpy(transferFlag, "PREPARE");
			write(sock, transferFlag, strlen(transferFlag));

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
					write(sock, transferFlag, strlen(transferFlag));

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
							write(sock, transferFlag, strlen(transferFlag));
						}
						else
						{
							puts("send complete");
							strcpy(transferFlag, "COMPLETE");
							write(sock, transferFlag, strlen(transferFlag));
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
	}
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