#########################################################################
# File Name: minit.sh
# Author: He Boxin
# mail: heboxin@pku.edu.cn
# Created Time: 2016年12月12日 星期一 20时18分09秒
#########################################################################
#!/bin/bash
gcc testIsend.c -o testIsend.o -c
gcc mpi.c -o mpi.o `pkg-config --cflags --libs glib-2.0` -c
gcc -o main testIsend.o mpi.o `pkg-config --cflags --libs glib-2.0` -lpthread

