/*
 * udp sender
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>

#define BUF_SIZE 4096

int main(int argc, char *argv[])
{
	char buf[BUF_SIZE];
	struct timeval start, end ;
	unsigned int repcount;
	unsigned long bytes ;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s port repcount\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	/* Obtain address(es) matching host/port */

	int sfd = socket(AF_INET, SOCK_DGRAM, 0) ;
	struct sockaddr_in servaddr ;

    // Filling server information
    servaddr.sin_family    = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(atoi(argv[1]));

    // Bind the socket with the server address
    if ( bind(sfd, (const struct sockaddr *)&servaddr,
            sizeof(servaddr)) < 0 )
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
	repcount=atoi(argv[2]);
	memset(buf,0,BUF_SIZE) ;
	gettimeofday(&start,NULL) ;
	for(int j=0; j<repcount; j += 1) {
		struct sockaddr_in cliaddr ;
	    int len = sizeof(cliaddr);  //len is value/result

	    int n = recvfrom(sfd, (char *)buf, BUF_SIZE,
	                MSG_WAITALL, ( struct sockaddr *) &cliaddr,
	                &len);
	    printf("buf[0] is %d\n", buf[0]) ;
	    fflush(stdout) ;
	    bytes += n ;
	}
	gettimeofday(&end, NULL);
	double duration=(end.tv_sec-start.tv_sec) + (end.tv_usec-start.tv_usec)*1e-6;
	unsigned long bytes=(unsigned long)BUF_SIZE*repcount;
	double bitrate=(bytes*8)/duration;
	printf("%lu bytes in %f seconds, rate=%f Gbit/sec\n", bytes, duration, bitrate*1e-9);

	close(sfd) ;

}
