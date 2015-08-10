#!/bin/sh
set -e

. ./common.sh

create_test_directories
start_chiron

echo "1234" > t3/hello
truncate -s 3 t3/hello

if [ "$(cat t3/hello)" != "123" ]; then
  echo "Truncation failed"
  clean_and_exit -1
else
  echo "Truncation ok"
fi

rm t3/hello
clean_and_exit 0
