#!/bin/sh
set -e

source ./common.sh

create_test_directories
start_chiron

mkdir t3/mysubdir
mkdir t3/mysubdir/subsubdir

ls -alR t3/mysubdir

if [ ! -d t1/mysubdir/subsubdir -o ! -d t2/mysubdir/subsubdir ]; then
  echo "Failed replication"
  clean_and_exit -1
fi

rmdir t3/mysubdir/subsubdir
rmdir t3/mysubdir
clean_and_exit 0
