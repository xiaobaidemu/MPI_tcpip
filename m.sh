#########################################################################
# File Name: m.sh
# Author: He Boxin
# mail: heboxin@pku.edu.cn
# Created Time: 2016年12月07日 星期三 15时09分20秒
#########################################################################
#!/bin/bash
gcc mpirun.c -Wall -o mpirun `pkg-config --cflags --libs glib-2.0`

