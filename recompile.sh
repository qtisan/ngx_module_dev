#!/bin/bash

ps aux | grep nginx | grep -v grep > /dev/null

if [ $? -eq 0 ]
then
    echo "Nginx is running, stopping it now, and then recompiling."
    sudo /usr/local/nginx/sbin/nginx -s stop
else
    echo "Nginx is not running, now recompiling."
fi

cd $WORKPLACE/nginx &&\
make modules OPTIMIZATION=O0 &&\
make install &&\

echo "[✓] module recompiled."

