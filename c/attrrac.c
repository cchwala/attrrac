#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "attrracd.h"

#define SERVER_ADDR "127.0.0.1"
//////////////////////////////////////////
// Use this to tunnel the connection	//
// ssh -L 1234:localhost:1111 root@sbc	//
//////////////////////////////////////////


int main(int argc, char *argv[])
{
	struct sockaddr_in strAddr;
	socklen_t lenAddr;
	int fdSock;
	//char message[MAX_LENGTH];
	
	/* open socket */
	if ((fdSock=socket(AF_INET, SOCK_STREAM, 0)) < 0){
		printf("Could not open socket\n");
		exit(1);
	}
	
	bzero(&strAddr, sizeof(strAddr));
	inet_pton(AF_INET, SERVER_ADDR, &strAddr);
	/* Set inet type socket */
	strAddr.sin_family = AF_INET;
	strAddr.sin_port = htons(1111);
	
	//strcpy(strAddr.sun_path, SOCKET_PATH);
	//lenAddr=sizeof(strAddr.sun_family)+strlen(strAddr.sun_path);
	lenAddr = sizeof(strAddr);
	
	/* Connect to socket */
	if (connect(fdSock, (struct sockaddr*)&strAddr, lenAddr) !=0 ){
		printf("Socket connection failed\n");
		exit(1);
	}
	printf("\nConnected to Server ... sending data ...\n");
		
	write(fdSock, argv[1], MAX_LENGTH);
	write(fdSock, argv[2], MAX_LENGTH);
	write(fdSock, argv[3], MAX_LENGTH);
	// add error checking.....
	//	read(fdSock, message, MAX_LENGTH);
	
	
	close(fdSock);
	return 0;
}
