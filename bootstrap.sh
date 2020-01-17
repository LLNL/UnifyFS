#!/bin/bash
#
# This is an easy-bake script to download and build all UnifyFS's dependencies.
#

ROOT="$(pwd)"

mkdir -p deps
mkdir -p install
INSTALL_DIR=$ROOT/install

cd deps

repos=(	https://xgitlab.cels.anl.gov/sds/bmi.git
	https://github.com/google/leveldb.git
	https://github.com/LLNL/GOTCHA.git
	https://github.com/pmodels/argobots.git
	https://github.com/mercury-hpc/mercury.git
	https://xgitlab.cels.anl.gov/sds/margo.git
	https://github.com/dvidelabs/flatcc.git
)

for i in "${repos[@]}" ; do
	# Get just the name of the project (like "mercury")
	name=$(basename $i | sed 's/\.git//g')
	if [ -d $name ] ; then
		echo "$name already exists, skipping it"
	else
		if [ "$name" == "mercury" ] ; then
			git clone --recurse-submodules $i
		else
			git clone $i
		fi
	fi
done

echo "### building bmi ###"
cd bmi
./prepare && ./configure --enable-shared --enable-bmi-only \
	--prefix="$INSTALL_DIR"
make -j $(nproc) && make install
cd ..

echo "### building leveldb ###"
cd leveldb
git checkout 1.22
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
	-DBUILD_SHARED_LIBS=yes ..
make -j $(nproc) && make install
cd ..
cd ..

echo "### building GOTCHA ###"
cd GOTCHA
# Unify won't build against latest GOTCHA, so use a known compatible version.
git checkout 0.0.2
mkdir -p build && cd build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ..
make -j $(nproc) && make install
cd ..
cd ..

echo "### building argobots ###"
cd argobots
git checkout v1.0rc2
./autogen.sh && CC=gcc ./configure --prefix="$INSTALL_DIR"
make -j $(nproc) && make install
cd ..

echo "### building mercury ###"
cd mercury
mkdir -p build && cd build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
      -DMERCURY_USE_BOOST_PP=ON \
      -DMERCURY_USE_CHECKSUMS=ON \
      -DMERCURY_USE_EAGER_BULK=ON \
      -DMERCURY_USE_SYSTEM_MCHECKSUM=OFF \
      -DNA_USE_BMI=ON \
      -DMERCURY_USE_XDR=OFF \
      -DBUILD_SHARED_LIBS=on ..
make -j $(nproc) && make install
cd ..
cd ..

echo "### building margo ###"
cd margo
git checkout v0.4.3
export PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig"
./prepare.sh
./configure --prefix="$INSTALL_DIR"
make -j $(nproc) && make install
cd ..

echo "### building flatcc ###"
cd flatcc
# need -DBUILD_SHARED_LIBS=ye
mkdir -p build && cd build
cmake -DBUILD_SHARED_LIBS=on -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DFLATCC_INSTALL=on ..
make -j $(nproc) && make install
cd ..
cd ..

cd "$ROOT"

echo "*************************************************************************"
echo "Dependencies are all built.  You can now build UnifyFS with:"
echo ""
echo -n "  export PKG_CONFIG_PATH=$INSTALL_DIR/lib/pkgconfig && "
echo "export LEVELDB_ROOT=$INSTALL_DIR && export FLATCC_ROOT=$INSTALL_DIR"
echo -n "  ./autogen.sh && ./configure --with-gotcha=$INSTALL_DIR"
echo " --prefix=$INSTALL_DIR"
echo "  make"
echo ""
echo "*************************************************************************"
