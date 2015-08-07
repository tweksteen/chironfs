#!/bin/sh
set -e

source ./common.sh

create_test_directories
start_chiron

echo "1234" > t3/hello
ln -s hello t3/mylink

if [ "$(readlink t3/mylink)" != "hello" ]; then
  echo "Failed link creation"
  clean_and_exit -1
fi

if [ ! -f t1/mylink -o ! -f t2/mylink ]; then
  echo "Failed replication"
  clean_and_exit -1
fi

if [ "$(cat t1/mylink)" != "1234" ]; then
  echo "Replica 1 failed"
  clean_and_exit -1
else
  echo "Replica 1 ok"
fi

if [ "$(cat t2/mylink)" != "1234" ]; then
  echo "Replica 2 failed"
  clean_and_exit -1
else
  echo "Replica 2 ok"
fi

rm t3/mylink
rm t3/hello
clean_and_exit 0
