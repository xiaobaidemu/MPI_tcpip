#########################################################################
# File Name: kill.sh
# Author: He Boxin
# mail: heboxin@pku.edu.cn
# Created Time: 2016年12月08日 星期四 10时57分38秒
#########################################################################
#!/bin/bash
ps  -ef|grep -v grep|grep mpirun|while read u p o
do
    kill -9 $p
done
ps  -ef|grep -v grep|grep main|while read u p o
do
    kill -9 $p
done
