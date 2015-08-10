#!/bin/sh
set -e

. ./common.sh

create_test_directories
start_chiron

echo "1234" > t3/hello

# Simulate a fault on t2
rm t2/hello

if [ "$(cat t3/hello)" != "1234" ]; then
  echo "Mountpoint failed"
  clean_and_exit -1
else
  echo "Mountpoint ok"
fi

rm t3/hello
clean_and_exit 0
