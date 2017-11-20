#########################################################################
# File Name: minit.sh
# Author: He Boxin
# mail: heboxin@pku.edu.cn
# Created Time: 2016年12月12日 星期一 20时18分09秒
#########################################################################
#!/bin/bash
gcc testBigIsend.c -o testBigIsend.o -c
gcc mpi.c -o mpi.o `pkg-config --cflags --libs glib-2.0` -c
gcc -o main testBigIsend.o mpi.o `pkg-config --cflags --libs glib-2.0` -lpthread

