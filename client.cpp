#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <dirent.h>
#include <queue>
#include <list>
#include <iostream>

using namespace std;
// First, assume there is no packet loss. Just have the client send a packet, and the server respond with an ACK, and so on.
// correct all _exit code!!!

#define PKTSIZE 524
#define HDSIZE 12
#define PLSIZE 512
#define SEQMAX 25600
#define RTO 500
#define WINDOW_SIZE 512
#define THRESHOLD 5120
#define SLOWSTART 1
#define CONGAVOID 2
#define FASTRETR 3
#define WINDOW_MAX 10240

int max(int a, int b) { if (a > b) return a; else return b; }

/* Congestion Control */
int cwnd = WINDOW_SIZE;
int ssthresh = THRESHOLD;
int cc_state = 1;
int acks[3];
//int sendBase;
//int nextSeqNum;

typedef struct header_proto{
	short seq, ackn;
	char ackf, syn, fin;
    short size;
    short padding;
} header_proto;

typedef struct Packet{
	header_proto hdout;
	char payload[PLSIZE];
} Packet;

void print_info(char in, header_proto* header, int cwnd, int ssthresh) //cwnd ssthresh DUP !!!
{
	if (in)
		printf("RECV");
	else
		printf("SEND");
	printf(" %d %d", header->seq, header->ackn);
	printf(" %d %d", cwnd, ssthresh);
	if (header->ackf)
		printf(" ACK");
	if (header->syn)
		printf(" SYN");
	if (header->fin)
		printf(" FIN");
	printf("\n");
	return;
}

void print_info(char in, Packet pktbuf, int cwnd, int ssthresh) //cwnd ssthresh DUP !!!
{
	if (in)
		printf("RECV");
	else
		printf("SEND");
	printf(" %d %d", pktbuf.hdout.seq, pktbuf.hdout.ackn);
	printf(" %d %d", cwnd, ssthresh);
	if (pktbuf.hdout.ackf)
		printf(" ACK");
	if (pktbuf.hdout.syn)
		printf(" SYN");
	if (pktbuf.hdout.fin)
		printf(" FIN");
	printf("\n");
	return;
}

header_proto* three_way_hs(int sockfd, struct sockaddr_in serv_addr)
{
	header_proto* header=(header_proto*)new char[12];
	short x = rand() % SEQMAX;
	memset(header, 0, HDSIZE);
	header->seq = x;
	header->syn = 1;
	
	int n = write(sockfd, header, HDSIZE);
	if (n < 0){
		perror("ERROR on writing to socket");
		_exit(1);
	}
	print_info(0, header, cwnd, ssthresh);
	bzero(header, HDSIZE);
	int len = sizeof(serv_addr);
	//n = recvfrom(sockfd, header, HDSIZE, MSG_DONTWAIT, (struct sockaddr*) &serv_addr, &len); //timeout!!!
	n = read(sockfd, header, HDSIZE);
	if (n < 0) {
		perror("ERROR reading from socket");
		_exit(1);
	}
	print_info(1, header, cwnd, ssthresh);
	if (header->ackf == 1 && header->syn == 1 && header->ackn == x+1)
		return header;
	else{
		fprintf(stderr, "ERROR: unable to complete 3-way handshake\n");
		_exit(1);
	}
}

char timeout(struct timeval start, struct timeval curr)
{
    struct timeval duration;
    timersub(&curr, &start, &duration);
	if ((duration.tv_sec > 10))
		return 10;
	if ((duration.tv_sec > 2))
		return 2;
	if ((duration.tv_sec != 0)||(duration.tv_usec > 0.5 * 1000000))
		return 1;
	return 0;
}

int last_three_ack(int ackn) {
	//printf("Received ACK: %d\n", ackn);
	acks[0] = acks[1];
	acks[1] = acks[2];
	acks[2] = ackn;
	if (acks[0] == acks[1] && acks[1] == acks[2]) {
		printf("Three duplicate acks: %d\n", acks[0]);
		return 1;
	}
	return 0;
}

void close_connection(int sockfd, short seqnum, struct sockaddr_in serv_addr){
	Packet pktbuf;
	///////////////////send fin
	header_proto* hdin = (header_proto*)new char[HDSIZE];
	struct timeval start_time, curr_time;
	bzero(&pktbuf, HDSIZE);
	pktbuf.hdout.seq = seqnum;
	pktbuf.hdout.fin = 1;
	int n = write(sockfd, &pktbuf, HDSIZE);
	if (n < 0){
		perror("ERROR on writing to socket");
		close(sockfd);
		_exit(1);
	}
	print_info(0, pktbuf, cwnd, ssthresh);

	///////////////////////Waiting for server's ACK
	bzero(hdin, HDSIZE);
	n = read(sockfd, hdin, HDSIZE);
	if (n < 0) {
		perror("ERROR reading from socket");
		_exit(1);
	}
	print_info(1, hdin, cwnd, ssthresh);
	if (hdin->ackf == 1)
	{
		gettimeofday(&start_time, NULL);
		while(1){
			gettimeofday(&curr_time, NULL);
			if(timeout(start_time, curr_time)==2)
			{
				bzero(&pktbuf, HDSIZE);
				pktbuf.hdout.seq = seqnum++;
				pktbuf.hdout.fin = 1;
				n = write(sockfd, &pktbuf, HDSIZE);
				if (n < 0){
					perror("ERROR on writing to socket");
					close(sockfd);
					_exit(1);
				}
				print_info(0, pktbuf, cwnd, ssthresh);
				gettimeofday(&start_time, NULL);
				continue;
			}

			bzero(hdin, HDSIZE);
			socklen_t len = sizeof(serv_addr);
			n = recvfrom(sockfd, hdin, HDSIZE, MSG_DONTWAIT, (struct sockaddr*) &serv_addr, &len);
			if (n <= 0)
				break;
			print_info(1, hdin, cwnd, ssthresh);
			//if (hdin->fin){
				bzero(&pktbuf, HDSIZE);
				pktbuf.hdout.seq = seqnum + 1;
				pktbuf.hdout.ackf = 1;
				pktbuf.hdout.ackn = hdin->seq + 1;
				n = write(sockfd, &pktbuf, HDSIZE);
				if (n < 0){
					perror("ERROR on writing to socket");
					close(sockfd);
					_exit(1);
				}
				print_info(0, pktbuf, cwnd, ssthresh);
				close(sockfd);
				_exit(0);
			//}
		}
	}
}

void sendfile(int sockfd, int input_file, struct sockaddr_in serv_addr)
{
	// prep
	int n, dupcount=0, counter, bytes_read = PLSIZE, window;
	Packet pktbuf;
	header_proto* hdin = (header_proto*)new char[HDSIZE];
	memset(hdin, 0, HDSIZE);
	memset(&pktbuf, 0, PKTSIZE);
	struct timeval start_time, curr_time, retrans_time;
	list<Packet> segments;
	queue<int> ack;

	header_proto* temp = three_way_hs(sockfd, serv_addr);
	*hdin = *temp;
	
	int sendbase = hdin->ackn; //window size!!!
	int seqnum = sendbase;
	int next_ackn = hdin->seq + 1;
	bool has_packet_to_ack = true;

	///////////////////keep sending file until finishes
	while (1)
	{
		counter = 0;
		int x = 0;
		//cout << "seqnum: " << seqnum << ", sendbase: " << sendbase << "\n";
		//cout << "ack: " << ack.size() << ", bytes_read: " << bytes_read << "\n";
		if (seqnum >= sendbase)
			window = sendbase + cwnd - seqnum;
		else if (seqnum < seqnum)
			window = sendbase + cwnd - SEQMAX - seqnum;
		if (bytes_read < PLSIZE)
		{
			close(input_file);
			close_connection(sockfd, seqnum, serv_addr);
			exit(0);
		}

		while (window >= PLSIZE)
		{
			bzero(&pktbuf, PKTSIZE);
			pktbuf.hdout.ackn = next_ackn;
			pktbuf.hdout.ackf = 1;
			if (!has_packet_to_ack)
				pktbuf.hdout.ackn = 0;
			has_packet_to_ack = false;
			pktbuf.hdout.seq = seqnum;
			bytes_read = read(input_file, pktbuf.payload, PLSIZE);
			if (bytes_read < 0){
				perror("ERROR on read the input file");
				close(input_file);
				_exit(1);
			}
			
			//////////////////////////// has packet, to send
			else if (bytes_read > 0)
			{
				pktbuf.hdout.size = bytes_read;
				seqnum+=bytes_read;
				window-=bytes_read;
				if (seqnum >= SEQMAX)
					seqnum-=SEQMAX;
				n = bytes_read;
				Packet *p = &pktbuf;
				while (n > 0)
				{
					int bytes_written = write(sockfd, p, (pktbuf.hdout.size)+HDSIZE);
					if (bytes_written <= 0){
						perror("ERROR on write");
						close(input_file);
						_exit(1);
					}
					print_info(0, pktbuf, cwnd, ssthresh);
					n -= bytes_written;
					p += bytes_written;
				}
				pktbuf.hdout.ackf = 0;
				pktbuf.hdout.ackn = 0;
				if (pktbuf.hdout.seq+bytes_read >= SEQMAX)
					ack.push(pktbuf.hdout.seq+bytes_read-SEQMAX);
				else
					ack.push(pktbuf.hdout.seq+bytes_read);
				segments.push_back(pktbuf);
			}

			//////////////////////// done reading, close connection
			else if (bytes_read == 0)
			{
				if (!ack.empty())
					break;
				close(input_file);
				close_connection(sockfd, seqnum, serv_addr);
				exit(0);
			}
		}

		



		///////////////////// Now read server response
		bool able_to_continue = false;
		gettimeofday(&start_time, NULL);
		gettimeofday(&retrans_time, NULL);
		while(1)
		{
			if (able_to_continue)
				break;
			gettimeofday(&curr_time, NULL);
			
			if (timeout(retrans_time, curr_time) == 1)
			{// retransmit all the packets in the window
				//cout <<"expecting: " << ack.front() << "\n";
				ssthresh = max(cwnd / 2, 1024);
				cwnd = 512;
				cc_state = SLOWSTART;
				list<Packet>::iterator it;
				for (it = segments.begin(); it != segments.end(); ++it){
					bzero(&pktbuf, PKTSIZE);
					pktbuf.hdout.seq = it->hdout.seq;
					strcpy(pktbuf.payload, it->payload);
					n = write(sockfd, &pktbuf, HDSIZE);
					if (n < 0){
						perror("ERROR on writing to socket");
						close(sockfd);
						_exit(1);
					}
					print_info(0, pktbuf, cwnd, ssthresh);
				}
				gettimeofday(&start_time, NULL);
			}
			if (timeout(start_time, curr_time) == 10)
			{
				close(sockfd);
				close(input_file);
				_exit(1);
			}
			
			bzero(hdin, HDSIZE);
			socklen_t len = sizeof(serv_addr);
			n = recvfrom(sockfd, hdin, HDSIZE, MSG_DONTWAIT, (struct sockaddr*) &serv_addr, &len);
			if (seqnum == sendbase)
				break;
			if (n <= 0 && able_to_continue)
				break;
			if (hdin->ackn != 0) // valid message
			{
				has_packet_to_ack = 1;
				gettimeofday(&start_time, NULL);
				if (hdin->ackn >= ack.front()){
					if (ack.empty())
						exit(1);
					able_to_continue = true;
					while(!ack.empty() && hdin->ackn != ack.front())
					{
						ack.pop();
						segments.pop_front();
					}
					ack.pop();
					segments.pop_front();
					next_ackn=hdin->seq+1;
					sendbase=hdin->ackn;
				}
				
				print_info(1, hdin, cwnd, ssthresh);

				if (cc_state == SLOWSTART) {
					if (cwnd + PLSIZE <= ssthresh)
						cwnd += PLSIZE;
					else
					{
						cc_state = CONGAVOID;
						cwnd += (512*512)/cwnd;
					}
				}
				else if (cc_state == CONGAVOID) {
					cwnd += (512*512)/cwnd;
				}
				if (cwnd > 10240)
					cwnd = 10240;
			}
		}
	}
	close(sockfd);
	if (input_file)
		close(input_file);
}







int main(int argc, char *argv[])
{
	int sockfd, portno, n;
	struct sockaddr_in serv_addr;
	struct hostent *server;
	if (argc < 4)
	{
		fprintf(stderr, "usage %s hostname port filename\n", argv[0]);
		_exit(0);
	}

	portno = atoi(argv[2]);	

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
	{
		perror("ERROR: opening socket");
		_exit(1);
	}

	server = gethostbyname(argv[1]);

	if (server == NULL || portno == 0)
	{
		fprintf(stderr, "ERROR: unable to find host or no such host\n");
		_exit(1);
	}

	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
	serv_addr.sin_port = htons(portno);

	/* Now connect to the server */
	if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("ERROR: connection failed");
		_exit(1);
	}

	int filefd = open(argv[3], O_RDONLY);

	sendfile(sockfd, filefd, serv_addr);
	return 0;
}