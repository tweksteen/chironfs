#!/bin/sh
set -e

. ./common.sh

create_test_directories
start_chiron

touch t3/hello2
echo "1234" > t3/hello
echo "4567" > t3/hello

rm t3/hello
rm t3/hello2
clean_and_exit 0

