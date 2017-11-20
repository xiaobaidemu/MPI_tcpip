/*************************************************************************
 > File Name: mpi.h
 > Author: He Boxin
 > Mail: heboxin@pku.edu.cn
 > Created Time: 2016年11月08日 星期三 15时36分55秒
 ************************************************************************/

#define MPI_ANY_SOURCE  (-2) //Fiala
#define MPI_ANY_TAG     (-1) //Fiala

// 6 line follow, neal
typedef int MPI_Datatype;
#define MPI_CHAR           ((MPI_Datatype)0x4c000101)
#define MPI_INT            ((MPI_Datatype)0x4c000405)

/* Communicators */
typedef int MPI_Comm;
#define MPI_COMM_WORLD ((MPI_Comm)0x44000000)

/* MPI request opjects */
typedef int MPI_Request;

typedef struct MPI_Status{
    int count;
    int cancelled;
    int MPI_SOURCE;
    int MPI_TAG;
    int MPI_ERROR;
} MPI_Status;


/* for info */
typedef int MPI_Info;

/* MPI's error classes */
#define MPI_SUCCESS          0      /* Successful return code */

/* Begin Prototypes */
int MPI_Init(int *, char ***);
int MPI_Finalize(void);
double MPI_Wtime(void);
double MPI_Wtick(void);
int MPI_Comm_size(MPI_Comm, int *);
int MPI_Comm_rank(MPI_Comm, int *);
int MPI_Isend(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int MPI_Irecv(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Request *);
int MPI_Recv(void*, int, MPI_Datatype, int, int, MPI_Comm, MPI_Status *);
int MPI_Test(MPI_Request *, int *, MPI_Status *);
int MPI_Wait(MPI_Request *, MPI_Status *);
