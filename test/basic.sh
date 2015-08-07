#!/bin/sh
set -e

source ./common.sh

create_test_directories
start_chiron

echo "1234" > t3/hello
if [ ! -f t1/hello -o ! -f t2/hello ]; then
  echo "Failed replication"
  clean_and_exit -1
fi

if [ "$(cat t1/hello)" != "1234" ]; then
  echo "Replica 1 failed"
  clean_and_exit -1
else
  echo "Replica 1 ok"
fi

if [ "$(cat t2/hello)" != "1234" ]; then
  echo "Replica 2 failed"
  clean_and_exit -1
else
  echo "Replica 2 ok"
fi

if [ "$(cat t3/hello)" != "1234" ]; then
  echo "Mountpoint failed"
  clean_and_exit -1
else
  echo "Mountpoint ok"
fi

rm t3/hello
clean_and_exit 0
