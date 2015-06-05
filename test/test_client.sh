#!/bin/bash
`ifstat -i eth0 > ifstat` &
sleep 1
ifstatpid=`ps aux  | grep "ifstat" | grep -v grep | awk '{print \$2}'`
for i in 1 2 4 8 16 32 64; do
    echo "GB: $i"
    /root/tcpclient $i 2 &
    pid=`ps aux  | grep "tcpclient" | grep -v grep | awk '{print \$2}'`
    `top -d 1 -p $pid -b | grep $pid &> cputop${i}` &
    sleep 1
    toppid=`ps aux  | grep "top -d 1 -p $pid" | grep -v grep | awk '{print \$2}'`
    while [ -e "/proc/$pid" ]; do
        sleep 1;
    done
    kill 15 $toppid
done
kill -15 $ifstatpid
ssh 8.8.8.12 "touch /root/testend"
ssh 8.8.8.13 "touch /root/testend"
ssh 8.8.8.1  "touch /root/hgshm/testend"
