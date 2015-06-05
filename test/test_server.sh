#!/bin/bash
rm -f /root/testend
/root/tcpserver 10000 4 &
sleep 1;
servpid=`ps aux | grep tcpserver | grep -v grep | awk '{print \$2}'`
`top -d 1 -p $servpid -b | grep $servpid &> cputop` &
`ifstat -i eth0 > ifstat` &
sleep 1
toppid=`ps aux  | grep "top -d 1 -p $servpid" | grep -v grep | awk '{print \$2}'`
ifstatpid=`ps aux  | grep "ifstat" | grep -v grep | awk '{print \$2}'`

echo $servpid $toppid $ifstatpid
while [ ! -e "/root/testend" ]; do
 sleep 1;
done
kill 15 $toppid $servpid $ifstatpid
