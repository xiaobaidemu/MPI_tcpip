/*************************************************************************
 > File Name: testBigIsend.c
 > Author: He Boxin
 > Mail: heboxin@pku.edu.cn
 > Created Time: 2016年12月28日 星期三 09时17分31秒
 ************************************************************************/

#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include"mpi.h"
#define SIZE 1024*1024*64
//#define SIZE 1024*16
int main(int argc, char **argv)
{
    int myid, numprocs;
    char *buffer_send, *buffer_recv;
    MPI_Init(NULL, NULL);
	MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &myid);
	MPI_Request request_send1, request_recv1;
	MPI_Status status;
    buffer_send = (char*)calloc(SIZE,sizeof(char));
    buffer_recv = (char*)malloc(SIZE);
	if(myid == 0)
	{
		MPI_Irecv(buffer_recv, SIZE, MPI_CHAR, 1, 123, MPI_COMM_WORLD, &request_recv1);
		MPI_Isend(buffer_send, SIZE, MPI_CHAR, 1, 456, MPI_COMM_WORLD, &request_send1);
	}
	if(myid == 1)
	{
		MPI_Irecv(buffer_recv, SIZE, MPI_CHAR, 0, 456, MPI_COMM_WORLD, &request_recv1);
		MPI_Isend(buffer_send, SIZE, MPI_CHAR, 0, 123, MPI_COMM_WORLD, &request_send1);
	}
	if(myid == 0)
	{
		MPI_Wait(&request_recv1, &status);
		printf("rank0 have recv the whole data.\n");
        MPI_Wait(&request_send1, &status);
        printf("rank0 have send the whole data.\n");
	}
	else
	{
		MPI_Wait(&request_recv1, &status);
		printf("rank1 have recv the whole data.\n");
        MPI_Wait(&request_send1, &status);
        printf("rank1 have send the whole data.\n");
	}
    MPI_Finalize();
    return 0;
    
}
