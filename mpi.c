/*************************************************************************
 > File Name: mpi.c
 > Author: He Boxin
 > Mail: heboxin@pku.edu.cn
 > Created Time: 2016年11月08日 星期三 16时55分54秒
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
//#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <sys/epoll.h>
#include <glib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "mpi.h"

//Communication defs
#define PORT_CALC 		(EXEC_PORT + 1)
#define DEFAULT_BACKLOG	5
#define CLIENT_ACK      "ACK_FROM_CLIENT"
#define BUFFER_LEN	 	1024
#define RECV_EOF  		-1
#define NA				-999
#define FD_SIZE         64
#define MAXEVENTS       64

//System Messages
#define NON_SYS				0
#define RECV_OK    			1
#define HANDSHAKE			2
#define	ISEND				3
#define SEND_REQ			4
#define BARRIER				6
#define BARRIER_GO			7
#define IRECV				10

//Envelope Structure
struct m
{
	int source;
	int dest;
	int tag;
	int comm;
	int sys;
	int size;
	void *msg;
};
typedef struct m msg;

//Communicator Structure
struct cmr
{
	int size;
	int rank;
	int parent;
	int lchild;
	int rchild;
};
typedef struct cmr communicator;
communicator C;

/*struct node
{
    int fd;
    int indexOfrank;//read from SC_TABLE[indexOfrank],indexOfrank == -1 means fd is DEFAULT_SOCKET
}
typedef struct node node;
*/
struct sockdata_
{
    int rank;//the rank in all process
    char* buffer_send;
    char* buffer_recv_withead;//used for recv the whole msg
    char* buffer_recv;//used for recv the msg without head
    int totalsize_s;//the total buffer size should be sent
    int restsendsize;//the rest size of buffer should be sent
    int totalsize_r;//the total buffer size should be received
    int haverecvsize;//the size of buffer have been received
    int handle_s;
    int tag_s;
};
typedef struct sockdata_ sockdata;

sockdata* SD[150];
//hashtable to store all sockdatas, each socket has one sockdata
//GHashTable *sd_table;
//prototypes
void *sub(void *arg);
GFunc print_msg_data(gpointer data, gpointer user_data);
int tsprintf(const char* format, ...);
msg *queue_find(int *GLOBAL, msg *msgPtr);
msg *asynch_queue_find(int *GLOBAL, msg *msgPtr);
char* construct_msg(int dest, int tag, int comm, int sys, int size, const char* buf);
ssize_t nonblock_send(int sockfd, const char* buffer, size_t buflen);
void add_event(int efd, int fd, sockdata* sd, int state)
{
    struct epoll_event event;
    event.data.ptr = (void*)sd;
    event.events = state;
    int rc = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event);
    error_check(rc, "add_event efd fail.");
    tsprintf("add_event efd success.\n");
}
void delete_event(int efd, int fd, sockdata* sd, int state)
{
    struct epoll_event event;
    event.data.ptr = (void*)sd;
    event.events = state;
    int rc = epoll_ctl(efd, EPOLL_CTL_DEL, fd, &event);
    error_check(rc, "delete_event efd fail.");
    tsprintf("delete_event efd success.\n");
}
void modify_event(int efd, int fd, sockdata* sd, int state)
{
    struct epoll_event event;
    event.data.ptr = (void*)sd;
    event.events = state;
    int rc = epoll_ctl(efd, EPOLL_CTL_MOD, fd, &event);
    error_check(rc, "modify_event efd fail.");
    tsprintf("modify_event efd success\n");
}

//be care of free
sockdata* initsockdata(int rank)
{
    sockdata* sd = (sockdata*)malloc(sizeof(sockdata));
    sd->rank = rank;
    sd->buffer_send = NULL;
    sd->buffer_recv_withead = NULL;
    sd->buffer_recv= NULL;
    sd->totalsize_s = 0;
    sd->restsendsize = 0;
    sd->totalsize_r = -1;
    sd->haverecvsize = 0;
    sd->handle_s = -1;
    sd->tag_s = -1;
    tsprintf("initsockdata...\n");
    return sd;
}

void setsockdata_s(sockdata* sd, char* buffer_send, int totalsize_s, int restsendsize, int handle_s, int tag_s)
{
    sd->buffer_send = buffer_send;
    sd->totalsize_s = totalsize_s;
    sd->restsendsize = restsendsize;
    sd->handle_s = handle_s;
    sd->tag_s = tag_s;
    tsprintf("setsockdata_s...\n");
}

void setsockdata_r(sockdata* sd, char* totalbuff, char* buffer_recv, int totalsize_r, int haverecvsize)
{
    sd->buffer_recv_withead = totalbuff; 
    sd->buffer_recv = buffer_recv;
    sd->totalsize_r = totalsize_r;
    sd->haverecvsize = haverecvsize;
    tsprintf("setsockdata_r...\n");
}

int make_socket_non_blocking (int sfd)
{
  int flags, s;

  flags = fcntl (sfd, F_GETFL, 0);
  if (flags == -1)
    {
      perror ("fcntl");
      return -1;
    }

  flags |= O_NONBLOCK;
  s = fcntl (sfd, F_SETFL, flags);
  if (s == -1)
    {
      perror ("fcntl");
      return -1;
    }

  return 0;
}


//lists and queues
GQueue *MSG_Q;

//Init Data
char EXEC_SERVER_NAME[50];
int EXEC_PORT;
//char **PHONEBOOK;
int MPI_RANK;
int LOCAL_PORT;
int NUMPROCS;
int DEFAULT_SOCKET;
int RC_TABLE[150];
int SC_TABLE[150];
int DEBUG;
struct timeval timev;//for debugging func, tsprintf

//thread shared variables
char PHONEBOOK[150][150];
int REQ_HANDLES[1000];
int ATTR[500][500];
int REQ_INDEX =				0;
int ACT_ISENDS =			0;
int ACT_BARRIER_MSG =		0;
int ACT_BARRIER_GO =		0;
int ACT_IRECVS =			0;

//thread synch variable
int DONE = 0;
pthread_t pEng;
pthread_mutex_t pEngL; //progress eng lock

int MPI_Init(int *argc, char ***argv)
{

    //printf("start init.\n");
	int new_socket, i, j, rc;
	int ack_len;
	char buf[BUFFER_LEN];

    //int efd;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	
	ack_len = strlen(CLIENT_ACK) + 1;
	
	MSG_Q = g_queue_new();

	DEBUG = atoi(getenv("DEBUG"));

	//SET Global MPI_EXEC info -- 
	strcpy(EXEC_SERVER_NAME, getenv("SERVER_NAME"));
	EXEC_PORT = atoi(getenv("PORT"));
	//SET rank and calculate port to listen on
	MPI_RANK= atoi(getenv("MPI_RANK"));
	//if(MPI_RANK == 0)
    C.rank = MPI_RANK;
	tsprintf("init begin\n");

	LOCAL_PORT =  MPI_RANK + PORT_CALC;
	
	new_socket = connect_to_server(EXEC_SERVER_NAME, EXEC_PORT);
    //tsprintf("new_socket:===========%d\n",new_socket);

	//send phonebook request and recieve ack &	
	//Set Global Number of Procceses	
	sprintf(buf, "%s", "phonebook_req");
	send_msg(new_socket, buf, strlen(buf) + 1);
	rc = recv_msg(new_socket, buf);
	NUMPROCS = atoi(buf);
	send_msg(new_socket, CLIENT_ACK, ack_len);

	for(i = 0; i < 150; i++)
	{
		for (j = 0; j < 150; j++)
		{
			PHONEBOOK[i][j] = '\0';
		}
	}

	for (i = 0; i < NUMPROCS; i++)
	{
		recv_msg(new_socket, buf);
		sprintf(PHONEBOOK[i], "%s", buf);
		send_msg(new_socket, CLIENT_ACK, ack_len);
	}

	//if (MPI_RANK == 0)
	//{
		for (i = 0; i < NUMPROCS; i++)
			tsprintf("print phonebook%s\n", PHONEBOOK[i]);
	//}


	//create the connection table and intialize it
	for(i = 0; i < NUMPROCS; i++)
	{
		RC_TABLE[i] = -1;
		SC_TABLE[i] = -1;
	}

	//set MPI_COMM_WORLDS communicator
	C.size = NUMPROCS;
	//C.rank = MPI_RANK;
	calc_tree(MPI_RANK, NUMPROCS,&C.parent,&C.lchild, &C.rchild);

	if (MPI_RANK == 0)
	{
		for(i = 0; i < 500; i++)
		{
			for(j = 0; j < 500; j++)
			{
				ATTR[i][j] = -1;
			}
		}
	}
	
	//setup listening socket
	DEFAULT_SOCKET = setup_to_accept(LOCAL_PORT);

	//if(C.rank == 0)
	tsprintf("here 1\n");

    //efd = epoll_create(FD_SIZE);
	tree_connect();
	
	//if(C.rank == 0)
	tsprintf("here 2\n");

	pthread_mutex_init(&pEngL, NULL);
	pthread_create(&pEng, NULL, sub, NULL );
	
	//if(C.rank == 0)
	tsprintf("finish init\n");
}

int MPI_Finalize(void)
{
	tsprintf("Finalize\n");
	MPI_Barrier(MPI_COMM_WORLD);
	while ( g_queue_get_length(MSG_Q) > 0 )
	{
		print_msgs();	
	}
	DONE = 1;
    printf("change done to the one!!!!!!!!!!!!!!!!!!\n");
	pthread_join(pEng, NULL);
    printf("pthread_join+++++++++++++++ .\n");

}

int MPI_Barrier(MPI_Comm comm)
{
	tsprintf("in barrier\n");
	tree_report(comm, BARRIER, &ACT_BARRIER_MSG, BARRIER_GO, &ACT_BARRIER_GO);
	tsprintf("out of barrier\n");
}


int MPI_Comm_size(MPI_Comm comm, int *size)
{
	*size = C.size;
	return MPI_SUCCESS;

}

int MPI_Comm_rank(MPI_Comm comm, int *rank)
{
	*rank = C.rank;
	return MPI_SUCCESS;
}

int MPI_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status)
{
	int i;
	msg toFind, *rp;
	tsprintf("RECV called for tag %d, source %d\n", tag, source);

	while(RC_TABLE[source] == -1)
	{
		//sleep(1);
	}

	//set msg fields to search in queue for
	toFind_set(&toFind, source, NA, tag, NA, NON_SYS, NA);
	rp = queue_find(NULL, &toFind);
	
	if (datatype == MPI_INT)
	{
		for (i = 0; i < count; i++)
		{
			sscanf(rp->msg + i * 10, "%010d", (int *)buf + i);
		}
	}
	else if (datatype == MPI_CHAR)
	{
		tsprintf("RECV %s\n", rp->msg);
		memmove(buf, rp->msg, rp->size);
	}

	if(status != NULL)
	{
		status->count = count;
		status->cancelled = 0;
		status->MPI_SOURCE = rp->source;
		status->MPI_TAG = rp->tag;
		status->MPI_ERROR = MPI_SUCCESS;
	}

	pthread_mutex_lock(&pEngL);
	g_queue_remove(MSG_Q, rp);
	pthread_mutex_unlock(&pEngL);	

}

int MPI_Isend(void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *handle)
{
	tsprintf("Begin ISEND\n");

	int i, rc, msgLen;
	int socket;
	int temp;
	char *intBuf, *sndBuf;
	msg *msgEnv, *isendEnv;

	if (datatype == MPI_INT)
	{
		intBuf = (char *)malloc(10 * count + 1);
		intBuf[0] = '\0';
		for (i = 0; i < count; i++)
		{			
			temp = *(int *)(buf + (i * sizeof(int)));
			sprintf(intBuf,"%s%010d",intBuf, temp);	
		}
		msgLen = strlen(intBuf);
		sndBuf = intBuf;
	}
	else if (datatype == MPI_CHAR)
	{
		msgLen = count;
		sndBuf = buf;
	}

	handshake(dest);

	pthread_mutex_lock(&pEngL);
	*handle = REQ_INDEX;
	REQ_HANDLES[REQ_INDEX] = 0;
	REQ_INDEX++;
	pthread_mutex_unlock(&pEngL);

	msgEnv = (msg *)malloc(sizeof(msg));
	msgEnv->source = C.rank;
	msgEnv->dest = dest;
	msgEnv->tag = tag;
	msgEnv->comm = comm;
	msgEnv->sys = NON_SYS;
	msgEnv->size = msgLen;
	msgEnv->msg = (char *)malloc(msgLen);
	memcpy(msgEnv->msg, sndBuf, msgLen);

	isendEnv = (msg *)malloc(sizeof(msg));
	isendEnv->source = 0;
	isendEnv->dest = 0;
	isendEnv->tag = *handle;
	isendEnv->comm = 0;
	isendEnv->sys = ISEND;
	isendEnv->msg = (void *)msgEnv;

	pthread_mutex_lock(&pEngL);
	g_queue_push_tail(MSG_Q, isendEnv);
	ACT_ISENDS++;
	tsprintf("Post-inc ACT_ISEND = %d\n", ACT_ISENDS);
	pthread_mutex_unlock(&pEngL);

}	

int MPI_Irecv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Request *handle)
{
	msg *msgPtr = (msg*)malloc( sizeof(msg) );
	msg *infoPtr= (msg*)malloc( sizeof(msg) );


	//do i need to lock this?
	pthread_mutex_lock(&pEngL);
	*handle = REQ_INDEX;
	REQ_HANDLES[REQ_INDEX] = 0;
	REQ_INDEX++;
	pthread_mutex_unlock(&pEngL);
	

	infoPtr->source = NA;
	infoPtr->dest = NA;
	infoPtr->tag = *handle;
	infoPtr->comm = NA;
	infoPtr->sys = datatype;
	infoPtr->msg = buf;
	infoPtr->size = count;


	msgPtr->source = source;
	msgPtr->dest = NA; //dest has no meaning so it holds the req value;
	msgPtr->tag = tag;
	msgPtr->comm = comm;
	msgPtr->sys = IRECV;
	msgPtr->msg = infoPtr;
	
	pthread_mutex_lock(&pEngL);
	g_queue_push_tail(MSG_Q, msgPtr);
	ACT_IRECVS++;
	pthread_mutex_unlock(&pEngL);


	tsprintf("Irecv hwp complete\n");
}

int MPI_Test(MPI_Request *handle, int *flag, MPI_Status *status)
{
	*flag = 0;

	if (REQ_HANDLES[*handle] != -1)
	{
		*flag = 1;
	}

}

int MPI_Wait(MPI_Request *handle, MPI_Status *status)
{
	tsprintf("MPI_wait on %d\n", *handle);
	while(!REQ_HANDLES[*handle])
	{}
}


double MPI_Wtime()
{
	struct timeval tv;

	gettimeofday(&tv, (struct timezone *) 0 );

	return ( tv.tv_sec + (tv.tv_usec / 1000000.0) );
}

double MPI_Wtick()
{
	struct timespec res;
	int rc;

	rc = clock_getres( CLOCK_REALTIME, &res );
	if (!rc) 
		return res.tv_sec + 1.0e-6 * res.tv_nsec;
}

//progress engine
void *sub(void *arg)
{
	int i, j, rc, n; 
    int destrank;
	int tcnum; //delete me
    int efd, indexofrank;
	int childCount, children[2];
	int new_socket;
	//fd_set read_fds; //used for new connections & incoming messages
	struct timeval tv;
	msg *hsEnv, *msgPtr, *isendPtr, *irecvPtr, *infoPtr;
	msg toFind, *rp;
	GList *listPtr = NULL;
    struct epoll_event event;
    struct epoll_event *events;

	//tsprintf("Progress Engine Initiated\n");

	tcnum = 0;
    efd = epoll_create(FD_SIZE);
    events = calloc(MAXEVENTS, sizeof(struct epoll_event));

    //event.data.fd = -DEFAULT_SOCKET - 1;
    sockdata default_sd = {-DEFAULT_SOCKET-1, NULL, NULL, NULL, 0, 0, -1, 0, -1, -1};
    event.data.ptr = &default_sd;
    event.events = EPOLLIN;
    rc = epoll_ctl(efd, EPOLL_CTL_ADD, DEFAULT_SOCKET, &event);

    if(C.parent != -1 && RC_TABLE[C.parent] != -1)
        add_event(efd, RC_TABLE[C.parent], SD[C.parent], EPOLLIN);
    if(C.lchild != -1 && RC_TABLE[C.lchild] != -1)
        add_event(efd, RC_TABLE[C.lchild], SD[C.lchild], EPOLLIN);
    if(C.rchild != -1 && RC_TABLE[C.rchild] != -1)
        add_event(efd, RC_TABLE[C.rchild], SD[C.rchild], EPOLLIN);
    
	while(!DONE)
	{	
        n = epoll_wait(efd, events, MAXEVENTS, 0);
        for(i = 0; i < n; i++)
		{
            if((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP))
            {
                int error = 0;
				socklen_t errlen = sizeof(error);
				if (getsockopt(RC_TABLE[((sockdata*)(events[i].data.ptr))->rank], 
                            SOL_SOCKET, SO_ERROR, (void *)&error, &errlen) == 0)
				{
					tsprintf("error = %s\n", strerror(error));
				} 
                continue;
            }
            else if(((sockdata*)(events[i].data.ptr))->rank < 0 && (events[i].events & EPOLLIN))
            {
                tsprintf("DEFAULT_SOCKET = %d.\n", -((sockdata*)(events[i].data.ptr))->rank - 1);
                hsEnv = (msg *)malloc( sizeof(msg) );
                new_socket = accept_connection(DEFAULT_SOCKET);

                read_env(new_socket, hsEnv);
                tsprintf("read handshake header\n");
                if (hsEnv->sys == HANDSHAKE)
                {
                    if(hsEnv->source == C.rank)
                    {
                        RC_TABLE[hsEnv->source] = new_socket;/////
                        add_event(efd, new_socket, SD[C.rank], EPOLLIN);
                    }
                    else
                    {
                        SC_TABLE[hsEnv->source] = RC_TABLE[hsEnv->source] = new_socket;/////
                        add_event(efd, new_socket, SD[hsEnv->source], EPOLLIN);
                        send_sys_msg(hsEnv->source, 0, 0, RECV_OK, 0, NULL);
                    }
                    tsprintf("proge eng Handshake with %d\n", hsEnv->source);
                }
                else
                {
                    tsprintf("Error - Non HS msg recv during HS\n");
                    tsprintf("hdr->sys = %d\n", hsEnv->sys);
                }
                //be care of the order of set socket to nonblocking
                rc = make_socket_non_blocking(new_socket);
				error_check(rc, "make_non_blocking error sub accpet");
                free(hsEnv);
            }
            else if(events[i].events & EPOLLIN)
            {
                sockdata* ptr = (sockdata*)(events[i].data.ptr);
                int srcrank = ptr->rank, readsize;
                //tsprintf("+++++++++++++++++++ptr->totalsize_r:%d\n", ptr->totalsize_r);
                //first we need to read 60 bytes msghead
                if(ptr->totalsize_r == -1)//&& ptr->buffer_recv == NULL
                {
                    //tsprintf("+++++++++++++++++++++++++++++++++++++++\n");
                    char* head;//remind to free!!!!!
                    int recvheadsize = ptr->haverecvsize;
                    if(ptr->buffer_recv == NULL)
                        head = (char*)malloc(60);
                    else
                        head = ptr->buffer_recv;
                    readsize = read(RC_TABLE[srcrank], head, 60 - recvheadsize);
                    if(readsize < 0)
                        dealrecvzero(&toFind, srcrank, head, NULL, recvheadsize);
                    else if(readsize == 0)
                    {
                        close(RC_TABLE[srcrank]);
                        RC_TABLE[srcrank] = -1;
                        free(head - recvheadsize);
                        tsprintf("normal closing %d when recv head\n", srcrank);
                    }
                    else if(readsize < 60 - recvheadsize){
                        setsockdata_r(SD[srcrank], NULL, head + readsize , -1,  recvheadsize + readsize);
                        //modify_event(efd, RC_TABLE[srcrank], SD[srcrank], EPOLLIN);
                        tsprintf("recv part head size ====== %d\n", recvheadsize + readsize);
                    }
                    else if(readsize == 60 - recvheadsize) 
                    {
                        msg* envmsg = (msg*)malloc(sizeof(msg));//remember to free!!!!!
                        //tsprintf("head == %s\n", head);
                        sscanf(head, "%010d%010d%010d%010d%010d%010d", &envmsg->source, &envmsg->dest, &envmsg->tag,
                                                                       &envmsg->comm, &envmsg->sys, &envmsg->size);
                        tsprintf("head have total received ===> s:%d d:%d t:%d c:%d sys:%d sz:%d\n", 
                                envmsg->source, envmsg->dest, envmsg->tag, envmsg->comm, envmsg->sys, envmsg->size);
                        process(envmsg->sys);
                        free(head - recvheadsize);
                        //begin to read realmsg
                        if(envmsg->size > 0)
                        {
                            envmsg->msg = malloc(envmsg->size);//remember to free
                            readsize = read(RC_TABLE[srcrank], envmsg->msg, envmsg->size);    
                            setsockdata_r(SD[srcrank], envmsg, envmsg->msg, 0, 0);
                            if(readsize < 0)
                                dealrecvzero(&toFind, srcrank, envmsg->msg, envmsg, 0);
                            else if(readsize == 0)
                            {
                                close(RC_TABLE[srcrank]);
                                RC_TABLE[srcrank] = -1;
                                free(envmsg->msg);
                                free(envmsg);
                                tsprintf("normal closing %d when recv real msg !!!!\n", srcrank);
                            }
                            else if(readsize < envmsg->size)
                            {
                                setsockdata_r(SD[srcrank], envmsg, envmsg->msg + readsize, envmsg->size, readsize);
                                tsprintf("have recv the part msg, size = %d\n", readsize);
                            }
                            else if(readsize == envmsg->size)
                            {//set the buffer_recv = null & totalsize_r = -1, haverecvsize = 0
                                pthread_mutex_lock(&pEngL);
						        g_queue_push_tail(MSG_Q, envmsg);
						        pthread_mutex_unlock(&pEngL);
                                setsockdata_r(SD[srcrank], NULL, NULL, -1, 0);
                                tsprintf("have recv the whole msg, size = %d\n", readsize);
                            }
                        }
                        else//直接将其放入队列
                        {
                            pthread_mutex_lock(&pEngL);
                            g_queue_push_tail(MSG_Q, envmsg);
                            pthread_mutex_unlock(&pEngL);
                            setsockdata_r(SD[srcrank], NULL, NULL, -1, 0);
                            tsprintf("push the msg with size=0 in to queue.\n");
                        }
                    }
                }
                else{
                    char* msg = ptr->buffer_recv;
                    int totalsize_r = ptr->totalsize_r;
                    int recvmsgsize = ptr->haverecvsize;
                    int readsize = read(RC_TABLE[srcrank], msg, totalsize_r - recvmsgsize);    
                    if(readsize < 0)
                        dealrecvzero(&toFind, srcrank, msg, ptr->buffer_recv_withead, recvmsgsize);
                    else if(readsize == 0)
                    {
                        close(RC_TABLE[srcrank]);
                        RC_TABLE[srcrank] = -1;
                        free(msg - recvmsgsize);
                        free(ptr->buffer_recv_withead);
                        tsprintf("normal closing %d when recv real msg =+++++++\n", srcrank);
                    }
                    else if(readsize < totalsize_r - recvmsgsize)
                    {
                        setsockdata_r(SD[srcrank], ptr->buffer_recv_withead, msg + readsize, totalsize_r, recvmsgsize + readsize);
                        tsprintf("have recv the part msg, size = %d\n", recvmsgsize + readsize);
                    }
                    else if(readsize == totalsize_r - recvmsgsize)
                    {//set the buffer_recv = null & totalsize_r = -1, haverecvsize = 0
                        pthread_mutex_lock(&pEngL);
                        g_queue_push_tail(MSG_Q, ptr->buffer_recv_withead);
                        pthread_mutex_unlock(&pEngL);
                        setsockdata_r(SD[srcrank], NULL, NULL, -1, 0);
                        tsprintf("have recv the whole msg, size = %d\n", recvmsgsize + readsize);
                    }
                }
            }
            else if(events[i].events & EPOLLOUT)
            {
                sockdata* ptr = (sockdata*)(events[i].data.ptr);
                if(ptr->buffer_send != NULL)
                    send_partmsg(efd, ptr->buffer_send, ptr->totalsize_s, ptr->restsendsize, ptr->rank, ptr->handle_s, ptr->tag_s);
            }

		}

        if(ACT_ISENDS > 0)
        {
            toFind_set(&toFind, NA, NA, NA, NA, ISEND, NA);
			tsprintf("looking for isend env\n");
			isendPtr = queue_find(NULL, &toFind);
			tsprintf("found isend env\n");
            msgPtr = isendPtr->msg;
            int destofrank = msgPtr->dest; 
            if(SD[destofrank]->buffer_send == NULL)
            {
                g_queue_remove(MSG_Q, isendPtr);
                ACT_ISENDS--;
                //construct the message will be send
                char *sendmsg = construct_msg(destofrank, msgPtr->tag, msgPtr->comm, NON_SYS, msgPtr->size, msgPtr->msg); 
                int totalsize = 60;
                if(msgPtr->size > 0)
                    totalsize += msgPtr->size;
                int sendsize = write(SC_TABLE[destofrank], sendmsg, totalsize); 
                if(sendsize < 0)
                {
                    if(errno != EAGAIN)
                        perror("sendmsg error");
                    else{
                        setsockdata_s(SD[destofrank], sendmsg, totalsize, totalsize, isendPtr->tag, msgPtr->tag);
                        if(destofrank != C.rank)
                            modify_event(efd, SC_TABLE[destofrank], SD[destofrank], EPOLLIN | EPOLLOUT);
                        else
                            add_event(efd, SC_TABLE[destofrank], SD[destofrank], EPOLLOUT);
                    }
                }
                else if(sendsize == totalsize)//free msg
                {
                    pthread_mutex_lock(&pEngL);
			        REQ_HANDLES[isendPtr->tag] = 1;
		        	pthread_mutex_unlock(&pEngL);
			        tsprintf("Isend complete for tag %d,have sendsize = %d\n", msgPtr->tag, sendsize);
                    free(sendmsg);
                }
                else if(sendsize < totalsize)//ser buufer_send not null & add_event epollout
                {
                   setsockdata_s(SD[destofrank], sendmsg + sendsize, totalsize, totalsize - sendsize, isendPtr->tag, msgPtr->tag);
                   if(destofrank != C.rank)
                        modify_event(efd, SC_TABLE[destofrank], SD[destofrank], EPOLLIN | EPOLLOUT);
                   else
                        add_event(efd, SC_TABLE[destofrank], SD[destofrank], EPOLLOUT);
                   tsprintf("have sendsize ====== %d\n", sendsize);
                }
            }
            //check whether SD[destrank]->buffer_send is NULL
        }

		if(ACT_IRECVS > 0)
		{
			//get the msg header that describes the message to irecv
			//and its .msg field will point to the another struct
			//that contains the datatype, count, and a buffer pointer
			toFind_set(&toFind, NA, NA, NA, NA, IRECV, NA);
			irecvPtr = queue_find(NULL, &toFind);
			infoPtr = irecvPtr->msg;

			//get the actual message to irecv
			toFind_set(&toFind, irecvPtr->source, NA, irecvPtr->tag, irecvPtr->comm, NON_SYS, NA);
			rp = asynch_queue_find(NULL, &toFind);

			if (rp)
			{
				if(infoPtr->sys == MPI_INT)
				{
					for (i = 0; i < infoPtr->size; i++)
					{
						sscanf(rp->msg + i * 10, "%010d", (int *)infoPtr->msg + i);
					}
				}
				else if(infoPtr->sys == MPI_CHAR)
				{
					memcpy(infoPtr->msg, rp->msg, infoPtr->size);
                    //printf("++++++++++++infoPtr++++++++++:%s\n",infoPtr->msg);
				}

				pthread_mutex_lock(&pEngL);
				REQ_HANDLES[infoPtr->tag] = 1;
				pthread_mutex_unlock(&pEngL);
	
				pthread_mutex_lock(&pEngL);
				free(infoPtr);
				g_queue_remove(MSG_Q, irecvPtr);
				g_queue_remove(MSG_Q, rp);
				pthread_mutex_unlock(&pEngL);		
			
				ACT_IRECVS--;
			}

		}	
		if (C.rank == 0 && tcnum % 100000 == 0)
		{
            tsprintf("tcnum:%d\n",tcnum);
			tsprintf("going around\n");
			
		}
		tcnum++;
		
	}
}
//deal with when readsize < 0
void dealrecvzero(msg* toFind, int srcrank, void* buff1, void* buff2, int recvsize)
{
    if(errno != EAGAIN);
    {
        tsprintf("something wrong happen in readenv\n");
        toFind_set(toFind, srcrank, NA, NA, NA, NA, NA);
        if (asynch_queue_find(NULL, toFind) == NULL)
        {   //delete_event(efd, RC_TABLE[indexofrank], SD[indexofrank], EPOLLIN);
            close(RC_TABLE[srcrank]);
            RC_TABLE[srcrank] = -1;
            tsprintf("abnormal closing %d\n", srcrank);
        }
        free(buff1 - recvsize);
        if(buff2 != NULL)
            free(buff2);
    }
    //else
        //setsockdata_r(SD[srcrank], buff, totalsize, recvsize);//0 mean headsize = 0
        //modify_event(efd, RC_TABLE[srcrank], SD[srcrank], EPOLLIN);
}
//must be free !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
char* construct_msg(int dest, int tag, int comm, int sys, int size, const char* buf)
{
    int msglen;
	char env[61], *message;
	sprintf(env, "%010d%010d%010d%010d%010d%010d", 
            C.rank, dest, tag, comm, sys, size);
    tsprintf("send msg ==> s:%d d:%d t:%d c:%d sys:%d sz:%d\n", 
                C.rank, dest, tag, comm, sys, size);
    
	if(size < 0)
		msglen = 60;
	else
		msglen = 60 + size;

	message = (char *)malloc(msglen);
	memcpy(message, env, 60);
	memcpy(message + 60, buf, size);


	if(SC_TABLE[dest] == -1)
	{
		tsprintf("********ERROR - Bad FD for %d\n", dest);
	}
    return message;
}

void process(int sys)
{	
	if(sys == BARRIER)
	{
		ACT_BARRIER_MSG++;
	}
	else if(sys == BARRIER_GO)
	{
		ACT_BARRIER_GO++;
	}
}


//treefunctions
calc_tree(int rank, int size, int *parent, int *lchild, int *rchild)
{
	int lchildEval = (2 * rank + 1);
	int rchildEval = (2 * rank + 2);
	
	if (rank == 0)
		*parent = -1;
	else 
		*parent = (rank-1) / 2;
	
	if (lchildEval > size - 1)
		*lchild = -1;
	else
		*lchild = lchildEval;
	
	if (rchildEval > size- 1)
		*rchild = -1;
	else
		*rchild = rchildEval;
}
tree_connect()
{
		int i, childCount = 0, fds[2], rc;

		childCount = 0;
		if(C.lchild != -1)
		{
			fds[childCount] = C.lchild;
			childCount++;
		}
		if(C.rchild != -1)
		{
			fds[childCount] = C.rchild;
			childCount++;
		}
		
        printf("childcount:%d\n",childCount);
		//wait for parent connection
		if (C.rank != 0)
		{
            tsprintf("rank %d  accept from parent %d\n", C.rank, C.parent);
			SC_TABLE[C.parent] = RC_TABLE[C.parent] = accept_connection(DEFAULT_SOCKET);
            //******************************************************************************
            SD[C.parent] = initsockdata(C.parent);
            //******************************************************************************
			rc = make_socket_non_blocking(RC_TABLE[C.parent]);
			error_check(rc, "make_non_blocking error tree connect accpet");
            //add_event(efd, RC_TABLE[C.parent], C.parent, EPOLLIN);

		}

		if (childCount > 0)
		{
			for(i = 0; i < childCount; i++)
			{
                tsprintf("rank %d  connect to child %d\n", C.rank, fds[i]);
				SC_TABLE[fds[i]] = RC_TABLE[fds[i]] = connect_to_server(PHONEBOOK[fds[i]], fds[i] + PORT_CALC);
                //add_event(efd, RC_TABLE[fds[i]], fds[i], EPOLLIN);
                //********************************************************
                SD[fds[i]] = initsockdata(fds[i]);
                //********************************************************
				rc = make_socket_non_blocking(RC_TABLE[fds[i]]);
				error_check(rc, "make_non_blocking error tree connect connect");
			}
		}
}
int count_children(int children[])
{	
	int childCount = 0;

	if(C.lchild != -1)
	{
		children[childCount] = C.lchild;
		childCount++;
	}
	if(C.rchild != -1)
	{
		children[childCount] = C.rchild;
		childCount++;
	}

	return childCount;
}

ssize_t nonblock_send(int sockfd, const char* buffer, size_t buflen)
{
  ssize_t tmp;
  size_t total = buflen;
  const char *p = buffer;
  while(1)
  {
    tmp = write(sockfd, p, total);
    if(tmp < 0)
    {
      if(errno == EINTR)
        return -1;
      if(errno == EAGAIN)
      {
        usleep(100);
        continue;
      }

      return -1;
    }

    if((size_t)tmp == total)
      return buflen;

    total -= tmp;
    p += tmp;
  }
  return tmp;
}
//[in] repVar -		status to report
//[in] actRepVar -	flag that indicates the message matching that status has been recieved by the root
//[in] goVar -		command from rank to continue
//[in] actGoVar -	flag indicating the command to go has arrived
int tree_report(MPI_Comm comm, int repVar, int *actRepVar, int goVar, int *actGoVar)
{
	int i, j, childCount, num, sum = 0;
	int children[2];
	msg *msgPtr;
	msg toFind, *rp;
		
	childCount = count_children(children);

	if (childCount == 0 && C.rank != 0)
	{
		num = 1;
		//tsprintf("TR leaf node sending %d to %d\n", num, C.parent);
		//send_sys_msg(C.parent, num, comm, repVar, 0, NULL);
        char*  headmsg = construct_msg(C.parent, num, comm, repVar, 0, NULL);
		if(nonblock_send(SC_TABLE[C.parent], headmsg, 60) == 60)
			free(headmsg);
		else
			perror("nonblock_send error");
	}
	else
	{
		i = 0;
		while (i < childCount)
		{
			toFind_set(&toFind, children[i], NA, NA, NA, repVar, NA);
			rp = queue_find(actRepVar, &toFind);
            tsprintf("queue_find barrier blocking until BARRIER be found......\n");
			sum += rp->tag;

			pthread_mutex_lock(&pEngL);
			g_queue_remove(MSG_Q, rp);
			pthread_mutex_unlock(&pEngL);		
			i++;
		}

		if (C.rank != 0)
		{
			sum++;
			//tsprintf("Sending bar inc var = %d to %d\n", sum, C.parent);
			char*  headmsg = construct_msg(C.parent, sum, comm, repVar, 0, NULL);
			if(nonblock_send(SC_TABLE[C.parent], headmsg, 60) == 60)
				free(headmsg);
			else
				perror("nonblock_send error");		
		}
	}

	if (C.rank == 0)
	{
		tsprintf("root starting Go message\n");
		for (i=0; i < childCount; i++)
		{
			char*  headmsg = construct_msg(children[i], NA, comm, goVar, 0, NULL);
			if(nonblock_send(SC_TABLE[children[i]], headmsg, 60) == 60)
				free(headmsg);
			else
				perror("nonblock_send error");
		}
        tsprintf("root finished Go message\n");

	}
	else
	{
			//tsprintf("waiting for go message\n");
			toFind_set(&toFind, NA, NA, NA, NA, goVar, NA);
			rp = queue_find(actGoVar, &toFind);
			
			pthread_mutex_lock(&pEngL);
			g_queue_remove(MSG_Q, rp);
			pthread_mutex_unlock(&pEngL);		

			//tsprintf("GO_MESSAGE Recieved\n");

			for (i=0; i < childCount; i++)
			{
				//tsprintf("sending go message to %d\n", children[i]);
				char*  headmsg = construct_msg(children[i], NA, comm, goVar, 0, NULL);
				if(nonblock_send(SC_TABLE[children[i]], headmsg, 60) == 60)
					free(headmsg);
				else
					perror("nonblock_send error");
			}
	}

	tsprintf("barrier out\n");
}

//messaging functions
read_msg(int fd, msg *env)
{
	int rc;
	
	if (env->size > 0)
	{
		env->msg = malloc(env->size);
		rc = recv_msg_by_size(fd, env->msg, env->size);
	}

}
int read_env(int fd, msg *env)
{
	char buf[10 * 6];
	int size, rc;

	env->msg = NULL;

	rc = recv_msg_by_size(fd, buf, 10 * 6);

	//tsprintf("read header rc = %d\n", rc);

	sscanf(buf, "%010d%010d%010d%010d%010d%010d", &env->source, &env->dest,
									   &env->tag, &env->comm, 
									   &env->sys, &env->size);

	if (rc != -1)
	{
		tsprintf("s:%d d:%d t:%d c:%d sys:%d sz:%d\n", env->source, env-> dest, env->tag, env->comm, env->sys, env->size);
	}

	return rc;
}
send_handshake_id(int dest, int fd)
{
	int i;
	char msgTag[60];

	sprintf(msgTag, "%010d%010d%010d%010d%010d%010d", C.rank, dest, 0,
												0, HANDSHAKE, 0);
    printf("send_handshake_id:::::::::::::%s\n",msgTag);
	send_msg(fd, msgTag, 6 * 10);	
}
send_sys_msg(int dest, int tag, int comm, int sys, int size, const char *buf)
{
	int i, msglen, rc;
	char env[61], *message;
	int adjSize;
	msg *toSelf;

	//tsprintf("send sys mesg\n");

	sprintf(env, "%010d%010d%010d%010d%010d%010d", C.rank, dest, tag,
												comm, sys, size);
	if(size < 0)
		adjSize = 0;
	else
		adjSize = size;

	msglen = 60 + adjSize;
	message = (char *)malloc(msglen);
	memcpy(message, env, 60);
	memcpy(message + 60, buf, adjSize);


	if(SC_TABLE[dest] == -1)
	{
		tsprintf("********ERROR - Bad FD for %d\n", dest);
	}
	send_msg(SC_TABLE[dest], message, msglen);	

	free(message);
}
void send_partmsg(int efd, char* sendmsg, int totalsize_s, int restsendsize, int destofrank, int handle_s, int tag_s )
{
    int sendsize = write(SC_TABLE[destofrank], sendmsg, restsendsize); 
    if(sendsize < 0)
    {
        if(errno != EAGAIN)
            perror("sendmsg error");
    }
    else if(sendsize == restsendsize)//set buffer_send null & remove epollout
    {
        setsockdata_s(SD[destofrank], NULL, 0, 0, -1, -1);
        if(destofrank != C.rank)
            modify_event(efd, SC_TABLE[destofrank], SD[destofrank], EPOLLIN);
        else
            delete_event(efd, SC_TABLE[destofrank], SD[destofrank], EPOLLOUT);
        pthread_mutex_lock(&pEngL);
        REQ_HANDLES[handle_s] = 1;
        pthread_mutex_unlock(&pEngL);
        tsprintf("send_partmsg: Isend complete for tag %d,have sendsize = %d\n", tag_s, totalsize_s);
        free(sendmsg + sendsize - totalsize_s);
    }
    else if(sendsize < restsendsize)//change buufer_send & modify_event epollout
    {
        setsockdata_s(SD[destofrank], sendmsg + sendsize, totalsize_s,  restsendsize - sendsize, handle_s, tag_s);
        /*if(destofrank != C.rank)
            modify_event(efd, SC_TABLE[destofrank], SD[destofrank], EPOLLIN | EPOLLOUT);//test whether cannot be modify
        else
            modify_event(efd, SC_TABLE[destofrank], SD[destofrank], EPOLLOUT);
            */
       tsprintf("send_partmsg: have sendsize ====== %d\n", totalsize_s - restsendsize + sendsize);
    }
}
send_msg(int fd, void *buf, int size)	
{
    int n;
	char msg[30];
    strcpy(msg, "send_msg error"); 
    n = write(fd, buf, size);
    tsprintf("nnnnnnnnnnnnnnnnnnnnnn= %d\n",n);
    error_check(n, msg);
}
int recv_msg(int fd, char *buf)
{
    int bytes_read;

    bytes_read = read(fd, buf, BUFFER_LEN);
    error_check( bytes_read, "recv_msg read");
    if (bytes_read == 0)
	return(RECV_EOF);
    return( bytes_read );
}
int recv_msg_by_size(int fd, void *buf, int size)
{
    int bytes_read = 0;
	int adjPtr = 0;

	while(bytes_read < size)
	{
        //tsprintf("bytes_read = %d\n",bytes_read);
		bytes_read += read(fd, buf + bytes_read, size);

		error_check( bytes_read, "recv_msg read");
		if (bytes_read == 0)
			return(RECV_EOF);
	}
    tsprintf("finished read.\n");

	return( bytes_read );
}
error_check(int val, char *str)	
{
    if (val < 0)
    {
		tsprintf("%s :%d: %s\n", str, val, strerror(errno));
		exit(1);
    }
}
handshake(int rank)
{
	int rc;
	msg env;
	int new_socket;
	
	if (SC_TABLE[rank] == -1)
	{
			//tsprintf("handshaking\n");
			new_socket = connect_to_server(PHONEBOOK[rank], rank + PORT_CALC);
			//rc = make_socket_non_blocking(new_socket);
			//error_check(rc, "make_non_blocking error handshake connect");
			send_handshake_id(rank, new_socket);
			if (rank == C.rank)
			{
				SC_TABLE[rank] = new_socket;         
                //***************************
                SD[rank] = initsockdata(C.rank); 
                //***************************
				//tsprintf("shake self\n");
				while (RC_TABLE[rank] == -1)
				{}
			}
			else
			{
				read_env(new_socket, &env);
				RC_TABLE[rank] = SC_TABLE[rank] = new_socket;
                //******************************
                SD[rank] = initsockdata(rank);
                //******************************
				tsprintf("read the hs response\n");
			}
            //be care of the order of set socket to nonblocking
            rc = make_socket_non_blocking(new_socket);
			error_check(rc, "make_non_blocking error handshake connect");
			tsprintf("handshake with %d complete\n", rank);
	}
}


//file descriptor setting functions
set_fds(fd_set *set, int *fds, int fdsNum, struct timeval *tv)
{
	int i;

	FD_ZERO(set);
	for (i = 0; i < fdsNum; i++)
	{
		FD_SET(fds[i], set);
	}
	tv->tv_sec = 0;
	tv->tv_usec = 0;
}
set_C_TABLE_fds(fd_set *set, struct timeval *tv)
{
	int i;
	FD_ZERO(set);

	for (i = 0; i < NUMPROCS; i++)
	{
		if (RC_TABLE[i] != -1)
		{
			FD_SET(RC_TABLE[i], set);
			//tsprintf("Setting %d\n", i);
		}
	}


	tv->tv_sec = 0;
	tv->tv_usec = 0;
}


//queue search functions
gint node_comp(gconstpointer a, gconstpointer b)
{
	int rc;
	const msg *ptrA = a;
	const msg *ptrB = b;

	if(ptrA->sys == NON_SYS && ptrB->sys != NA)
	{
		rc = 1;

		if(ptrB->source == MPI_ANY_SOURCE && ptrB->tag == MPI_ANY_TAG)
		{
			rc =  0;
		}
		else if (ptrB->source == MPI_ANY_SOURCE)
		{
			if(ptrB->tag == ptrA->tag)
				rc = 0;		
		}
		else if (ptrB->tag == MPI_ANY_TAG)
		{
			if(ptrB->source == ptrA->source)
				rc = 0;
		}
		else if (ptrB->source == ptrA->source && ptrB->tag == ptrA->tag)
		{
				rc = 0;
		}	
	}
	else
	{
		rc = 0;

		if(ptrB->source != NA && ptrB->source != ptrA->source)
		{
			//tsprintf("source wrong\n");
			rc++;
		}
		if(ptrB->dest != NA && ptrB->dest != ptrA->dest)
		{
			//tsprintf("dest wrong\n");
			rc++;
		}
		if(ptrB->tag != NA && ptrB->tag != ptrA->tag)
		{			
			//tsprintf("tag wrong\n");
			rc++;
		}
		if(ptrB->comm != NA && ptrB->comm != ptrA->comm)
		{
			//tsprintf("comm wrong\n");
			rc++;
		}
		if(ptrB->sys != NA && ptrB->sys != ptrA->sys)
		{
			//tsprintf("sys wrong\n");
			rc++;
		}
		if(ptrB->size != NA && ptrB->size != ptrA->size)
		{
			rc++;
		}
	}
	return rc;
}

//IN - GLOBAL - the shared global variable that communicates with the
//Progress Engine Thread.  Otherwise it is NULL.
//IN - msgPtr - is a copy of the msg node to search for in the queue
//OUT - rPtr - is a pointer to the matching node in the queue
msg *queue_find(int *GLOBAL, msg *msgPtr)
{
	GList *listPtr = NULL; 
	int dummyGlobal = 1;

	//if there is no global value to check
	//just use the dummy var to cause the loop to execute
	if(!GLOBAL)
		GLOBAL = &dummyGlobal; 

	while(!listPtr)
	{
		if (*GLOBAL > 0)
		{
			pthread_mutex_lock(&pEngL);
			if( g_queue_get_length(MSG_Q) > 0 )
			{
				//if (C.rank == 0)
				//tsprintf("finding %d %d\n", msgPtr->source, msgPtr->sys);
				listPtr = g_queue_find_custom(MSG_Q, msgPtr,(GCompareFunc)node_comp);
			}
			pthread_mutex_unlock(&pEngL);
		}
	}
	*GLOBAL--;

	return (msg *)listPtr->data; //convert pointer to a list to a pointer to a msg struct
}
msg *asynch_queue_find(int *GLOBAL, msg *msgPtr)
{
	GList *listPtr = NULL; 
	int dummyGlobal = 1;
	msg *rp = NULL;

	//if there is no global value to check
	//just use the dummy var to cause the loop to execute
	if(!GLOBAL)
		GLOBAL = &dummyGlobal; 

	if(!listPtr)
	{
		if (*GLOBAL > 0)
		{
			pthread_mutex_lock(&pEngL);
			if( g_queue_get_length(MSG_Q) > 0 )
			{
				listPtr = g_queue_find_custom(MSG_Q, msgPtr,(GCompareFunc)node_comp);
			}
			pthread_mutex_unlock(&pEngL);
		}
	}


	if(listPtr)
	{
		*GLOBAL--;
		rp = listPtr->data;
	}

	//tsprintf("aqf ptr = %p\n", rp);

	return rp; //convert pointer to a list to a pointer to a msg struct
}
toFind_set(msg *toFind, int source, int dest, int tag, int comm, int sys, int size)
{
	bzero( (void *)toFind, sizeof(msg) );

	if (source == NA)
		toFind->source = NA;
	else
		toFind->source = source;

	if (dest == NA)
		toFind->dest = NA;
	else
		toFind->dest = dest;

	if (tag == NA)
		toFind->tag = NA;
	else
		toFind->tag = tag;

	if (comm == NA)
		toFind->comm = NA;
	else
		toFind->comm = comm;

	if (sys == NA)
		toFind->sys = NA;
	else
		toFind->sys = sys;

	if (size == NA)
		toFind->size = NA;
	else
		toFind->size = size;
}


//debug functions
void print_msgs()
{
	tsprintf("*******************************\n");
	tsprintf("Queue size = %d\n", g_queue_get_length(MSG_Q));
	g_queue_foreach(MSG_Q, (GFunc)print_msg_data, NULL);
	tsprintf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n\n");
}
GFunc print_msg_data(gpointer data, gpointer user_data)
{
	msg * ptr = data;

	tsprintf("ptr->source = %d\n",(msg *)ptr->source);
	tsprintf("ptr->dest = %d\n", (msg *)ptr->dest);
	tsprintf("ptr->tag = %d\n", (msg *)ptr->tag);
	//tsprintf("ptr->comm = %d\n", (msg *)ptr->comm);
	tsprintf("ptr->sys = %d\n", (msg *)ptr->sys);
	//tsprintf("ptr->size = %d\n",(msg *)ptr->size);
	tsprintf("ptr->msg = %s\n", (msg *)ptr->msg);
	tsprintf("\n");
}
int tsprintf(const char* format, ...)
{
	if (DEBUG)
	{
		va_list args;
		char buf[100];

		va_start(args, format);
		vsprintf(buf, format, args);
		va_end(args);

		gettimeofday(&timev, NULL);
		printf("%d.%ld - [%d] - %s",timev.tv_sec, timev.tv_usec, C.rank, buf);
	}
}


//butler functions
connect_to_server(char *hostname, int port)	
{
    int i, rc, client_socket;
    int optval = 1,optlen;
    struct sockaddr_in listener;
    struct hostent *hp;

    hp = gethostbyname(hostname);
    if (hp == NULL)
    {
		printf("connect_to_server: gethostbyname %s: %s -- exiting\n",
		hostname, strerror(errno));
		exit(99);
    }

    bzero((void *)&listener, sizeof(listener));
    bcopy((void *)hp->h_addr, (void *)&listener.sin_addr, hp->h_length);
    listener.sin_family = hp->h_addrtype;
    listener.sin_port = htons(port);

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    error_check(client_socket, "net_connect_to_server socket");

    rc = connect(client_socket,(struct sockaddr *) &listener, sizeof(listener));
	if (rc == -1)
	{
		//printf("Connect with RC -1\n");
		for (i = 0; i < 20; i++)
		{
			tsprintf("Retry connect to %d\n", i + 1);
			if (connect(client_socket,(struct sockaddr *) &listener, sizeof(listener)) == 0)
			{
				//tsprintf("Gotit\n");
				break;
			}
			else
				sleep(i/4);
		}
	}
	error_check(client_socket, "net_connect_to_server connect");

	setsockopt(client_socket, SOL_SOCKET, SO_REUSEADDR, (char*) &optval, sizeof(optval));	
    setsockopt(client_socket,IPPROTO_TCP,TCP_NODELAY,
	(char *)&optval,sizeof(optval));


    return(client_socket);
}
setup_to_accept(int port)	
{
    int rc, default_socket;
    struct sockaddr_in sin, from;
    int optvalue = 1;

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    default_socket = socket(PF_INET, SOCK_STREAM, 0);
    error_check(default_socket,"setup_to_accept socket");

    rc = setsockopt(default_socket, SOL_SOCKET, SO_REUSEADDR, (char*) &optvalue, sizeof(optvalue));	
    error_check(rc, "SockOpt");	
	
    rc = bind(default_socket, (struct sockaddr *)&sin ,sizeof(sin));
    error_check(rc,"setup_to_accept bind");		
	
    rc = listen(default_socket, DEFAULT_BACKLOG);
    error_check(rc,"setup_to_accept listen");
	
	return(default_socket);
}
int accept_connection(int default_socket )	
{
    int fromlen, new_socket, gotit;
    int optval = 1, optlen;
    struct sockaddr_in from;

    fromlen = sizeof(from);
    gotit = 0;
    while (!gotit)
    {
		new_socket = accept(default_socket, (struct sockaddr *)&from, &fromlen);
		if (new_socket == -1)
		{
		    /* Did we get interrupted? If so, try again */
		    if (errno == EINTR)
				continue;
		    else
				error_check(new_socket, "accept_connection accept");
		}
		else
		    gotit = 1;
   	}


	setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, (char*) &optval, sizeof(optval));	
    setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&optval, sizeof(optval));

	//tsprintf("NS = %d\n", new_socket);
    return(new_socket);
}
