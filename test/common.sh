#!/bin/sh
set -e

chironfs_pid=0

function clean_up {
  kill $chironfs_pid
  wait $chironfs_pid
  rm -rf t1 t2 t3
  [ -f test.log ] &&  rm test.log
}

function clean_and_exit {
  clean_up
  exit $1
}

function create_test_directories {
  mkdir t1 t2 t3
}

function start_chiron {
  ../src/chironfs -f --log ./test.log t1=t2 t3 &
  chironfs_pid=$!
  echo "Chironfs PID is $chironfs_pid"
  sleep 1
}

