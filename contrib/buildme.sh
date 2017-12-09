#!/bin/bash
set -x

cd ..

#export PATH=`pwd`/autotools/install/bin:$PATH
export PKG_CONFIG_PATH=`pwd`/deps/install/pkgconfig

make distclean

./autogen.sh
if [ $? -ne 0 ] ; then
  echo "autogen failed"
  exit 1
fi
#exit 0

export CFLAGS="-g -O0"

installdir=`pwd`/install
rm -rf $installdir

./configure --prefix=$installdir
if [ $? -ne 0 ] ; then
  echo "configure failed"
  exit 1
fi

make clean
if [ $? -ne 0 ] ; then
  echo "make clean failed"
  exit 1
fi

make
if [ $? -ne 0 ] ; then
  echo "make failed"
  exit 1
fi

make install
if [ $? -ne 0 ] ; then
  echo "make install failed"
  exit 1
fi
