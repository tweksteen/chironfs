#!/bin/sh
source ./common.sh

create_test_directories
start_chiron

touch t3/hello
chmod 755 t3/hello
ls -alh t3
echo "1234" >> t3/hello
chmod 444 t3/hello
ls -alh t3
echo "5678" >> t3/hello 2>/dev/null

if [ "$(cat t3/hello)" != "1234" ]; then
  echo "chmod failed"
  clean_and_exit -1
else
  echo "chmod ok"
fi

chmod 777 t3/hello
rm t3/hello
clean_and_exit 0
