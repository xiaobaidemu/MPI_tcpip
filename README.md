# MPI_tcpip
> 用socket和epoll，select来模拟MPI的send,recv,Isend,Irecv，barrier等原语
> 
> 用法和效果和mpi相似，由mpirun在各个节点上启动程序，此版本是C语言版本，封装性不好，之后会用C++进行重新封装，并且所使用的数据类型也会进行修改
