#!/bin/bash
export PATH="/usr/tce/packages/gcc/gcc-4.9.3/bin:$PATH"
export PATH="/usr/tce/packages/mvapich2/mvapich2-2.2-gcc-4.9.3/bin:$PATH"
echo $PATH
. /usr/tce/packages/dotkit/init.sh
set -x

topdir=`pwd`
installdir=$topdir/install
export PATH="${topdir}/autotools/install/bin:$PATH"
aclocal; autoheader; automake; autoconf
./autogen.sh

rm -rf build
mkdir -p build
cd build

export PKG_CONFIG_PATH="${installdir}/lib/pkgconfig"

# TODO: avoid this step
# necessary so configure test of dtcmp links with MPI
export CC=mpicc
export CXX=mpic++
echo $CC
# hack to get things to build after common library
export CFLAGS="-g O0"
#export LDFLAGS="-Wl,-rpath,${topdir}/install/lib -L${topdir}/install/lib -lcircle"

#  --enable-experimental \
cd ..
configure \
  --with-mdhim=/g/g0/sikich1/unifycr/UnifyCR_Meta/src \
  --prefix=$installdir \
  --with-leveldb=/g/g0/sikich1/leveldb-1.14 \
make clean
make VERBOSE=1 && \
make VERBOSE=1 install
if [ $? -ne 0 ] ; then
  echo "failed to configure, build, or install unifycr_server"
  exit 1
fi
