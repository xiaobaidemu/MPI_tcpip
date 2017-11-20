/*************************************************************************
 > File Name: testinit.c
 > Author: He Boxin
 > Mail: heboxin@pku.edu.cn
 > Created Time: 2016年12月08日 星期四 12时17分53秒
 ************************************************************************/

#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include"mpi.h"

int main(int argc,char **argv)
{
    MPI_Init(NULL,NULL);
    int size,rank;
    MPI_Comm_size(MPI_COMM_WORLD,&size);
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
	MPI_Request request_send1,request_send2, request_recv1,request_recv2;
    MPI_Status status;
    printf("This is process %d in all %d processes.\n",rank,size);
    char buffer_recv1[20],buffer_recv2[20], buffer_send1[20],buffer_send2[20];
    if(rank==0)
    {
        memcpy(buffer_send1,"hello p1 i p0.\n",20);
        memcpy(buffer_send2,"hello again to p1.\n",20);
    }
    else
    {
        memcpy(buffer_send1,"hello p0 i p1.\n",20);
        memcpy(buffer_send2,"hello again to p0.\n",20);
    }
	/*if(rank == 0)
    {
        MPI_Isend(buffer_send,20,MPI_CHAR,1,123,MPI_COMM_WORLD,&request_send);
        MPI_Irecv(buffer_recv1,20,MPI_CHAR,1,123,MPI_COMM_WORLD,&request_recv1);
        MPI_Irecv(buffer_recv,20,MPI_CHAR,1,456,MPI_COMM_WORLD,&request_recv2);
    }
    if(rank == 1)
    {
        MPI_Isend(buffer_send,20,MPI_CHAR,0,456,MPI_COMM_WORLD,&request_send);
        MPI_Irecv(buffer_recv1,20,MPI_CHAR,0,456,MPI_COMM_WORLD,&request_recv1);
        MPI_Irecv(buffer_recv,20,MPI_CHAR,0,123,MPI_COMM_WORLD,&request_recv2);
    }*/
    if(rank == 0)
    {
        //MPI_Irecv(buffer_recv,20,MPI_CHAR,1,123,MPI_COMM_WORLD,&request_recv1);
        MPI_Irecv(buffer_recv1,20,MPI_CHAR,1,456,MPI_COMM_WORLD,&request_recv1);
        MPI_Isend(buffer_send1,20,MPI_CHAR,1,123,MPI_COMM_WORLD,&request_send1);
        MPI_Isend(buffer_send2,20,MPI_CHAR,1,789,MPI_COMM_WORLD,&request_send2);
        MPI_Irecv(buffer_recv2,20,MPI_CHAR,1,789,MPI_COMM_WORLD,&request_send2);
    }
    if(rank == 1)
    {
        //MPI_Irecv(buffer_recv,20,MPI_CHAR,0,456,MPI_COMM_WORLD,&request_recv1);
        MPI_Irecv(buffer_recv1,20,MPI_CHAR,0,123,MPI_COMM_WORLD,&request_recv1);
        MPI_Isend(buffer_send1,20,MPI_CHAR,0,456,MPI_COMM_WORLD,&request_send1);
        MPI_Isend(buffer_send2,20,MPI_CHAR,0,789,MPI_COMM_WORLD,&request_send2);
        MPI_Irecv(buffer_recv2,20,MPI_CHAR,0,789,MPI_COMM_WORLD,&request_send2);        
    }
    /*
	if(rank == 0)
    {
        
        MPI_Isend(buffer_send,20,MPI_CHAR,1,123,MPI_COMM_WORLD,&request_send);
        MPI_Isend(buffer_send,20,MPI_CHAR,2,123,MPI_COMM_WORLD,&request_send1);
        printf("Isend +++++++++++++++++++ to rank 2.\n");
        MPI_Irecv(buffer_recv,20,MPI_CHAR,1,456,MPI_COMM_WORLD,&request_recv2);
    }
    if(rank == 1)
    {
        MPI_Isend(buffer_send,20,MPI_CHAR,0,456,MPI_COMM_WORLD,&request_send);
        MPI_Isend(buffer_send,20,MPI_CHAR,2,456,MPI_COMM_WORLD,&request_send1);
        printf("Isend ------------------- to rank 2.\n");
        MPI_Irecv(buffer_recv,20,MPI_CHAR,0,123,MPI_COMM_WORLD,&request_recv2);
    }*/
	//MPI_Wait(&request_send,&status);
	//MPI_Wait(&request_recv2, &status);
    if(rank == 1||rank == 0)
    {
		MPI_Wait(&request_send1,&status);
		MPI_Wait(&request_recv1, &status);
        printf("rank %d:%s",rank,buffer_recv1);
        MPI_Wait(&request_send2,&status);
		MPI_Wait(&request_recv2, &status);
        printf("rank %d:%s",rank,buffer_recv2);
    }  
    MPI_Finalize();
    //sleep(3);
    return 0;
}
