#!/bin/bash
set -x

cd ..

rm -rf deps
mkdir -p deps
pushd deps
  installdir=`pwd`/install
  rm -rf $installdir
  
  tarname=v1.20
  if [ ! -f ${tarname}.tar.gz ] ; then
    wget https://github.com/google/leveldb/archive/${tarname}.tar.gz
    tar -zxf ${tarname}.tar.gz
  fi
  
  pushd leveldb-1.20
    make
  
    echo ""
    echo "Copying leveldb to $installdir..."
    echo ""
  
    mkdir -p $installdir/include
    mkdir -p $installdir/lib64
    mkdir -p $installdir/pkgconfig
  
    cp -pr include/leveldb   $installdir/include/.
    cp -pr out-*/libleveldb* $installdir/lib64/.
  
    cp ../../contrib/leveldb.pc $installdir/pkgconfig/.
    sed -i "s#__INSTALLDIR__#$installdir#g" $installdir/pkgconfig/leveldb.pc
  popd

  if [ ! -d GOTCHA-0.0.2 ] ; then
    if [ ! -f 0.0.2.tar.gz ] ; then
      wget https://github.com/LLNL/GOTCHA/archive/0.0.2.tar.gz
    fi
    tar -zxf 0.0.2.tar.gz
  fi
  
  pushd GOTCHA-0.0.2
    cmake -DCMAKE_INSTALL_PREFIX=$installdir
    make
    make install
  popd
popd
