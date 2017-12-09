#!/bin/bash
set -x

cd ..

mkdir -p autotools
cd autotools

installdir=`pwd`/install

# add autotools install bin to our path
export PATH=${installdir}/bin:$PATH

# build autoconf
if [ ! -f autoconf-2.69.tar.gz ] ; then
  wget http://ftp.gnu.org/gnu/autoconf/autoconf-2.69.tar.gz
fi
rm -rf autoconf-2.69
tar -zxf autoconf-2.69.tar.gz
pushd autoconf-2.69
  ./configure --prefix=$installdir
  make
  make install
popd

# build automake
if [ ! -f automake-1.15.tar.gz ] ; then
  wget http://ftp.gnu.org/gnu/automake/automake-1.15.tar.gz
fi
rm -rf automake-1.15
tar -zxf automake-1.15.tar.gz
pushd automake-1.15
  ./configure --prefix=$installdir
  make
  make install
popd

# build libtool
if [ ! -f libtool-2.4.6.tar.gz ] ; then
  wget http://mirror.team-cymru.org/gnu/libtool/libtool-2.4.6.tar.gz
fi
rm -rf libtool-2.4.6
tar -zxf libtool-2.4.6.tar.gz
pushd libtool-2.4.6
  ./configure --prefix=$installdir
  make
  make install
popd
