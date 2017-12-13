#!/bin/bash
set -x

# requires at least gcc v4.9.3

if [ ! -d astyle ] ; then
  if [ ! -f astyle_3.0_linux.tar.gz ] ; then
    wget https://downloads.sourceforge.net/project/astyle/astyle/astyle%203.0/astyle_3.0_linux.tar.gz
  fi
  tar -zxf astyle_3.0_linux.tar.gz
fi

pushd astyle/src
  make -f ../build/gcc/Makefile
popd
