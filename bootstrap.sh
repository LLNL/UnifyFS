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
	https://github.com/LLNL/GOTCHA.git
	https://github.com/pmodels/argobots.git
	https://github.com/mercury-hpc/mercury.git
	https://xgitlab.cels.anl.gov/sds/margo.git
)

if [ "$1" = "--with-leveldb" ] ; then
    repos+=(https://github.com/google/leveldb.git)
fi

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


# Assuming automake version starts with 1.#, collect value after initial "1."
automake_sub_version=$(automake --version | head -n1 |
                   sed 's/[^.]*\.\([0-9]\+\).*/\1/')

# If automake decimal version is < 1.15, install needed versions of autotools
if [ "$automake_sub_version" -lt "15" ]; then
    echo "### detected automake version is older than 1.15 ###"
    echo "### building required autotools ###"

    # Get system name from current automake to avoid using outdated config.guess
    sys_name=$(find /usr/share/automake* -name "config.guess" -exec {} \;)

    # Add autotools install to PATH
    export PATH=${INSTALL_DIR}/bin:$PATH

    autotools_repos=(http://ftp.gnu.org/gnu/autoconf/autoconf-2.69.tar.gz
        http://ftp.gnu.org/gnu/automake/automake-1.15.tar.gz
        http://mirror.team-cymru.org/gnu/libtool/libtool-2.4.2.tar.gz
        https://pkg-config.freedesktop.org/releases/pkg-config-0.27.1.tar.gz
)

    # wget and untar autotools
    for j in "${autotools_repos[@]}" ; do
        # Get tarball name and project-version name
        tar_name=$(basename $j)
        name=$(echo $tar_name | sed 's/\.tar.gz//g')
        if [ -f $tar_name ] ; then
            echo "$tar_name already exists, skipping wget"
        else
            wget $j
        fi
        rm -rf $name
        tar -zxf $tar_name
    done

    # build autoconf
    echo "### building autoconf ###"
    pushd autoconf-2.69
        ./configure --build=$sys_name --prefix=$INSTALL_DIR
        make
        make install
    popd

    # build automake
    echo "### building automake v1.15 ###"
    pushd automake-1.15
        ./configure --prefix=$INSTALL_DIR
        make
        make install
    popd

    # build libtool
    echo "### building libtool ###"
    pushd libtool-2.4.2
        ./configure --build=$sys_name --prefix=$INSTALL_DIR
        make
        make install
    popd

    # build pkg-config
    echo "### building pkg-config ###"
    pushd pkg-config-0.27.1
        ./configure --build=$sys_name --prefix=$INSTALL_DIR
        make
        make install
    popd
else
    echo "### detected automake version is greater than or equal to 1.15 ###"
    echo "### skipping autotools build ###"
fi


echo "### building bmi ###"
cd bmi
./prepare && ./configure --enable-shared --enable-bmi-only \
	--prefix="$INSTALL_DIR"
make -j $(nproc) && make install
cd ..

if [ "$1" = "--with-leveldb" ] ; then
    echo "### building leveldb ###"
    cd leveldb
    git checkout 1.22
    mkdir -p build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DBUILD_SHARED_LIBS=yes ..
    make -j $(nproc) && make install
    cd ..
    cd ..
else
    echo "### skipping leveldb build ###"
fi

echo "### building GOTCHA ###"
cd GOTCHA
# Unify won't build against latest GOTCHA, so use a known compatible version.
git checkout 1.0.3
mkdir -p build && cd build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ..
make -j $(nproc) && make install
cd ..
cd ..

echo "### building argobots ###"
cd argobots
git checkout v1.0
./autogen.sh && CC=gcc ./configure --prefix="$INSTALL_DIR"
make -j $(nproc) && make install
cd ..

echo "### building mercury ###"
cd mercury
git checkout v1.0.1
git submodule update --init
mkdir -p build && cd build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
      -DMERCURY_USE_SELF_FORWARD=ON \
      -DMERCURY_USE_BOOST_PP=ON \
      -DMERCURY_USE_CHECKSUMS=ON \
      -DMERCURY_USE_EAGER_BULK=ON \
      -DMERCURY_USE_SYSTEM_MCHECKSUM=OFF \
      -DNA_USE_BMI=ON \
      -DMERCURY_USE_XDR=OFF \
      -DBUILD_SHARED_LIBS=ON ..
make -j $(nproc) && make install
cd ..
cd ..

echo "### building margo ###"
cd margo
git checkout v0.4.3
export PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig:$PKG_CONFIG_PATH"
./prepare.sh
./configure --prefix="$INSTALL_DIR" --enable-shared
make -j $(nproc) && make install
cd ..

cd "$ROOT"

echo "*************************************************************************"
echo "Dependencies are all built.  You can now build UnifyFS with:"
echo ""
if [ "$automake_sub_version" -lt "15" ] ; then
    echo -n "  export PKG_CONFIG_PATH=/usr/share/pkgconfig:"
    echo "/usr/lib64/pkgconfig:$INSTALL_DIR/lib/pkgconfig:\$PKG_CONFIG_PATH"
    echo "  export PATH=$INSTALL_DIR/bin:\$PATH"
else
    echo " export PKG_CONFIG_PATH=$INSTALL_DIR/lib/pkgconfig:\$PKG_CONFIG_PATH"
fi
if [ "$1" = "--with-leveldb" ] ; then
    echo "export LEVELDB_ROOT=$INSTALL_DIR"
fi
echo -n "  ./autogen.sh && ./configure --with-gotcha=$INSTALL_DIR"
echo " --prefix=$INSTALL_DIR"
echo "  make"
echo ""
echo "*************************************************************************"
