/*************************************************************************
 > File Name: /home/heboxin/Desktop/Comm1/mpirun.c
 > Author: He Boxin
 > Mail: heboxin@pku.edu.cn
 > Created Time: 2016年11月01日 星期日 13时21分48秒
 ************************************************************************/
#include <sys/select.h>
#include <stdio.h>
#include <unistd.h> 
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <malloc.h>
#include <errno.h>
#include <wait.h>
#include <glib.h>

#define false 0
#define true 1
#define READ 0
#define WRITE 1
#define RECV_OK    		0
#define RECV_EOF  		-1
#define DEFAULT_BACKLOG		5
#define NON_RESERVED_PORT 	8854
#define BUFFER_LEN	 	1000	
#define SERVER_ACK		"ACK_FROM_SERVER"
#define MAXEVENTS 64
#define FD_SIZE 64

/* Macros to convert from integer to net byte order and vice versa */
#define i_to_n(n)  (int) htonl( (u_long) n)
#define n_to_i(n)  (int) ntohl( (u_long) n)

void server(int port);
int setup_to_accept(int port)	;
int accept_connection(int default_socket);	
void send_phonebook(int new_socket, char *phonebook[], int numProcs);
char *arg_cat(int argc, char *argv[], int argstart);
int send_msg(int fd, char *buf, int size);	

const int LLEN = 256;

int main(int argc,char *argv[])
{
    FILE *infile, *fp[LLEN];
	int i, j, n, len, offset, nread, read_total, rc, closed = 0;
	int numProcs = 1, numHosts;
	int lineNum = 0, debug = 0, port;
	int default_socket, new_socket;
	int read_fds[LLEN];
	int fds[LLEN];
	char *phonebook[LLEN];
	char buff[1000];
	char env[256];
	char ho_name[LLEN][LLEN];
	char line[LLEN];
	char hostname[50];
	char *args;
	struct timeval tv;

	//related with epoll
	int efd;
	struct epoll_event event;
	struct epoll_event *events;

    fd_set readSet;

	setbuf(stdout,NULL);	
	
	gethostname(hostname, 50);
	
	if (argc > 0)
	{
		while ( (rc = getopt(argc, argv, "dn:p:")) != -1) 
		{
            printf("(%d)  ",optind);
			switch (rc) 
			{
				case 'n':
					numProcs = atoi(optarg);
					break;
				case 'p':
					port = atoi(optarg);
					break;
				case 'd':
					debug = 1;
					break;
				case '?':
					break;
				default:
					printf ("?? getopt returned character code 0%o ??\n",  rc);
			}
		}
    
		if (optind < argc) 
		{
            printf("sssssssssssssssssss optind:%d\n", optind);
            for(int i = 0; i < argc;i++)
            {
                printf("%s\n", argv[i]);
            }
			args = arg_cat(argc, argv, optind);
            printf("%s\n", args);
            printf("eeeeeeeeeeeeeeeeeeeee\n");
		}
	}
	
	
	//initialize line buffer and hostname matrix
	for(i = 0; i < LLEN; i++)
	{
		for (j = 0; j < LLEN; j++)
		{
			ho_name[i][j] = '\0';			
		}
	}
	for (i = 0; i < LLEN; i++)
	{
		line[i] = '\0';
		read_fds[i] = 0;
	}

	//Open hostnames file and read in names
	if ((infile = fopen("hostnames", "r")) == NULL)
	{
		printf("open failed for file: %s\n",argv[1]);
		exit(99);
	}
	i = numHosts = 0;
	while (fgets(line, sizeof line, infile) != NULL)
	{
		len = strlen(line);
		if (line[len-1] == '\n')
		{
			line[len-1] = '\0';
		}
		strcpy(ho_name[i], line);
		i++;
        printf("hostip %d:%s\n",i,ho_name[i-1]);
	}
	numHosts = i;

	//create string from command and popen it 
	for (i = 0; i < numProcs; i++)
	{
		snprintf(env, 1000," 'export DEBUG=%d;export MPI_RANK=%d;export MPI_SIZE=%d;export SERVER_NAME=%s;export PORT=%d; %s'", debug, i, numProcs, hostname, port, args);
		snprintf(buff, 1000,"ssh %s %s 2>&1", ho_name[i % (numHosts)], env);
        //printf("%s\n",buff);
		phonebook[i] = malloc(strlen(ho_name[i % numHosts]));
		strcpy(phonebook[i], ho_name[i % numHosts]);
		fp[i] = popen(buff, "r");
		fds[i] = fileno(fp[i]);
        printf("fds:%d\n",fds[i]);
		if (debug)
			printf("%s\n", buff); 

	}

	if (debug)
		printf("...popen()s completed\n");
		
	default_socket = setup_to_accept(port);	
    printf("finish listen...\n");	
	//create epolling
	efd = epoll_create(FD_SIZE);
	error_check(efd, "epoll_create efd");
	
	events = calloc(MAXEVENTS, sizeof(event));

    event.data.fd = default_socket;
	event.events = EPOLLIN;
	rc = epoll_ctl(efd, EPOLL_CTL_ADD, default_socket, &event);
	error_check(rc, "epoll_ctl_add default_socket");
    
    FD_ZERO(&readSet);
    for(i =0; i < numProcs; i++)
    {
        FD_SET(fds[i],&readSet);
    }
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    rc = select(FD_SETSIZE, &readSet, NULL, NULL, &tv);
	while(closed < numProcs)
	{
		for (i = 0; i  < numProcs ; i++)
		{
			if (FD_ISSET(fds[i], &readSet) == 1)
			{
				j = read(fds[i], buff, BUFFER_LEN);
				if (j < 0)
				{
                    
				}
				else if (j == 0)
				{
					close_fp(fp[i]);
					read_fds[i] = 1;
					closed++;
                    printf("close--------------------\n");
				}
				else
				{
					if (j < BUFFER_LEN)
						buff[j] = '\0';
					printf("%s", buff);
				}
			}	
		}

		FD_ZERO(&readSet);
		for (i = 0; i < numProcs; i++)
		{
			if (read_fds[i] == 0)
				FD_SET(fds[i], &readSet);
		}
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		
		rc = select(FD_SETSIZE, &readSet, NULL, NULL, &tv);

		n = epoll_wait(efd, events, MAXEVENTS, 0);
		for(i = 0;i < n;i++)
		{
            printf("start_wait....\n");
			if ((events[i].events & EPOLLERR) ||
              	(events[i].events & EPOLLHUP) ||
              	(!(events[i].events & EPOLLIN)))
        		{
          			fprintf (stderr, "epoll efd error\n");
          			close (events[i].data.fd);
                    continue;
        		}
            else if(events[i].data.fd == default_socket)
            {
                new_socket = accept_connection(default_socket);
                event.data.fd = new_socket;
                event.events = EPOLLIN;
                rc = epoll_ctl(efd, EPOLL_CTL_ADD, new_socket, &event);
                printf("accpet %d\n",new_socket);
                error_check(rc, "epoll_etl_add new_socket");
                continue;
            }
            else
            {
                printf("start read from events[i].data.fd\n");
                read_total = read(events[i].data.fd, buff, BUFFER_LEN);
                printf("finish read from events[i].data.fd\n");
                if(strcmp(buff, "phonebook_req") == 0)
                {
                    if((rc = fork()) == -1)
                    {
                        printf("server: fork failed\n");
                        exit(99);
                    }
                    else if (rc > 0)//Parent Process no longer monitor this fd
                    {
                        printf("parent\n");
                        event.data.fd = events[i].data.fd;
                        event.events = EPOLLIN;
                        rc = epoll_ctl(efd, EPOLL_CTL_DEL, events[i].data.fd, &event);
                        error_check(rc,"epoll_ctl_delete parent process delete");
                        close(events[i].data.fd);
                    }
                    else         //Child process 
                    {

                        close(default_socket);
                        send_phonebook(events[i].data.fd, phonebook, numProcs);
                        printf("finish send_phonebook\n");
                        exit(0);
                    }
                }
            }
        }
	}
}

void close_fp(FILE* fp)
{
    int rc = pclose(fp);
    if(rc == -1)
    {
        perror("close file pointer wrong.\n");
        exit(1);
    }
    else
    {
        printf("child process status [%d].", rc);
        if(WIFEXITED(rc) != 0)
            printf("return status [%d].", WEXITSTATUS(rc));
        if(WIFSIGNALED(rc))
            printf("signal finish [%d].", WTERMSIG(rc));
        printf("\n");
    }
}
char *arg_cat(int argc, char *argv[], int argstart)
{
	int length = 0;
	int i;

	for (i = argstart; i < argc; ++i)
    	length += strlen(argv[i]);

	char *output = (char*)malloc(length + 1);

	char *dest = output;
	for (i = argstart; i < argc; ++i) 
	{
    	char *src = argv[i];
	    while (*src)
        *dest++ = *src++;
	    if (i < argc - 1)
			*dest++ = ' ';
	}
	dest = '\0';
	
	return output;

}

//adds the process rank to before each line of output

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
int setup_to_accept(int port)	
{
    printf("start to accept.\n");
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

	//rc = make_socket_non_blocking(default_socket);
	//error_check(rc,"make_socket_non_blocking");
	
    rc = listen(default_socket, DEFAULT_BACKLOG);
    error_check(rc,"setup_to_accept listen");
	
	return(default_socket);
}

int accept_connection(int default_socket )	
{
    int fromlen, new_socket, gotit;
    int optval = 1,optlen;
    struct sockaddr_in from;
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    fromlen = sizeof(from);
    gotit = 0;
    while (!gotit)
    {
		new_socket = accept(default_socket, (struct sockaddr *)&from, &fromlen);
		if (new_socket == -1)
		{
		    /* Did we get interrupted? If so, try again */
		    if ((errno == EINTR)) 
				continue;
		    else
				error_check(new_socket, "accept_connection accept");
		}
		else
		    gotit = 1;
   	}
    int rc = getnameinfo(&from, fromlen, hbuf, sizeof(hbuf), sbuf, 
            sizeof(sbuf),NI_NUMERICHOST | NI_NUMERICSERV);
    if(rc == 0)
    {
        printf("Accepted connection on descriptor %d (host=%s,port=%s)\n",
                new_socket, hbuf, sbuf);
    }
    //rc = make_socket_non_blocking(new_socket);
    //error_check(rc,"make_socket_non_blocking new_socket");

    setsockopt(new_socket,IPPROTO_TCP,TCP_NODELAY,(char *)&optval,sizeof(optval));
    return(new_socket);
}

void send_phonebook(int new_socket, char *phonebook[], int numProcs)
{
	int i, ack_len, len;
	char buf[100];
		
	ack_len = strlen(SERVER_ACK) + 1;

	//send number of processes and recv ack
	len = sprintf(buf, "%d", numProcs) + 1;
	i =	send_msg(new_socket, buf, len);
	recv_msg(new_socket, buf);

	//send numProcs
	for (i = 0; i < numProcs; i++)
	{
		sprintf(buf, "%s", phonebook[i]);
		send_msg(new_socket, buf, strlen(buf)+1);
		recv_msg(new_socket, buf);
	}


}

error_check(int val, char *str)	
{
    if (val < 0)
    {
	printf("%s :%d: %s\n", str, val, strerror(errno));
    printf("error.\n");
	exit(1);
    }
}

int send_msg(int fd, char *buf, int size)	
{
    int n;

    n = write(fd, buf, size);
    error_check(n, "send_msg write");
	return n;
}

int recv_msg(int fd, char *buf)
{
    int bytes_read;

    bytes_read = read(fd, buf, BUFFER_LEN);
    error_check( bytes_read, "serv recv_msg read");
    if (bytes_read == 0)
	return(RECV_EOF);
    return( bytes_read );
}

