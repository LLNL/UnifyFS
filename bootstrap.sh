#!/bin/bash
#
# This is an easy-bake script to download and build all UnifyFS's dependencies.
#

make_nproc=4

ROOT="$(pwd)"

mkdir -p deps
mkdir -p install

INSTALL_DIR=$ROOT/install
export PATH=$INSTALL_DIR/bin:$PATH
export LD_LIBRARY_PATH=$INSTALL_DIR/lib:$INSTALL_DIR/lib64:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=$INSTALL_DIR/lib/pkgconfig:$INSTALL_DIR/lib64/pkgconfig:$PKG_CONFIG_PATH

cd deps

repos=(https://github.com/LLNL/GOTCHA.git
    https://github.com/pmodels/argobots.git
    https://github.com/mercury-hpc/mercury.git
    https://xgitlab.cels.anl.gov/sds/margo.git
    https://github.com/pmodels/openpa.git
    https://github.com/ecp-veloc/spath.git
)

# optional builds
use_bmi=0
use_old_margo=0

while [ $# -ge 1 ]; do
    case "$1" in
      "--with-bmi" )
        use_bmi=1 ;;
      "--with-old-margo" )
        use_old_margo=1 ;;
      *)
        echo "USAGE ERROR: unknown option $1" ;;
    esac
    shift
done

if [ $use_old_margo -eq 1 ]; then
    argobots_version="v1.0"
    libfabric_version="v1.9.1"
    mercury_version="v1.0.1"
    margo_version="v0.4.3"
else
    argobots_version="v1.0.1"
    libfabric_version="v1.11.1"
    mercury_version="v2.0.0"
    margo_version="v0.9"
    repos+=(https://github.com/json-c/json-c.git)
fi

if [ $use_bmi -eq 0 ]; then
    mercury_bmi="OFF"
    mercury_ofi="ON"
    repos+=(https://github.com/ofiwg/libfabric.git)
else
    mercury_bmi="ON"
    mercury_ofi="OFF"
    repos+=(https://xgitlab.cels.anl.gov/sds/bmi.git)
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


echo "### building GOTCHA ###"
cd GOTCHA
# Unify won't build against latest GOTCHA, so use a known compatible version.
git checkout 1.0.3
mkdir -p build && cd build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ..
make -j $make_nproc && make install
cd ..
cd ..

echo "### building openpa ###"
cd openpa
git clean -fdx
git checkout v1.0.4
./autogen.sh
mkdir -p build
cd build
../configure --prefix="$INSTALL_DIR"
make -j $make_nproc && make install
cd ../..

echo "### building argobots ###"
cd argobots
git checkout $argobots_version
./autogen.sh && CC=gcc ./configure --prefix="$INSTALL_DIR"
make -j $make_nproc && make install
cd ..

if [ $use_bmi -eq 1 ]; then
    echo "### building bmi ###"
    cd bmi
    ./prepare && ./configure --prefix="$INSTALL_DIR" \
        --enable-shared --enable-bmi-only
    make -j $make_nproc && make install
    cd ..

    echo "### skipping libfabric build ###"
else
    echo "### skipping bmi build ###"

    echo "### building libfabric ###"
    cd libfabric
    git clean -fdx
    git checkout $libfabric_version
    ./autogen.sh
    mkdir -p build
    cd build
    ../configure --prefix="$INSTALL_DIR" \
        --enable-sockets --enable-rxm --enable-tcp
    make -j $make_nproc && make install
    cd ../..
fi

echo "### building mercury ###"
cd mercury
git checkout $mercury_version
git submodule update --init
mkdir -p build && cd build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
      -DMERCURY_USE_SELF_FORWARD=ON \
      -DMERCURY_USE_BOOST_PP=ON \
      -DMERCURY_USE_CHECKSUMS=ON \
      -DMERCURY_USE_EAGER_BULK=ON \
      -DMERCURY_USE_SYSTEM_MCHECKSUM=OFF \
      -DNA_USE_BMI=$mercury_bmi \
      -DNA_USE_OFI=$mercury_ofi \
      -DMERCURY_USE_XDR=OFF \
      -DBUILD_SHARED_LIBS=ON ..
make -j $make_nproc && make install
cd ../..

if [ $use_old_margo -eq 0 ]; then
    echo "### building json-c ###"
    cd json-c
    git checkout json-c-0.15-20200726
    mkdir -p build && cd build
    cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ..
    make -j $make_nproc && make install
    cd ../..
else
    echo "### skipping json-c build ###"
fi

echo "### building margo ###"
cd margo
git checkout $margo_version
./prepare.sh
./configure --prefix="$INSTALL_DIR" --enable-shared
make -j $make_nproc && make install
cd ..

echo "### building spath ###"
cd spath
git checkout master
mkdir -p build && cd build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DMPI=OFF ..
make -j $make_nproc && make install
cd ../..

cd "$ROOT"

echo "*************************************************************************"
echo "Dependencies are all built.  You can now build UnifyFS with:"
echo ""
if [ "$automake_sub_version" -lt "15" ] ; then
    echo -n "  export PKG_CONFIG_PATH=/usr/share/pkgconfig:/usr/lib64/pkgconfig:"
    echo "$INSTALL_DIR/lib/pkgconfig:\$PKG_CONFIG_PATH"
    echo "  export PATH=$INSTALL_DIR/bin:\$PATH"
else
    echo -n "  export PKG_CONFIG_PATH=$INSTALL_DIR/lib/pkgconfig:"
    echo "$INSTALL_DIR/lib64/pkgconfig:\$PKG_CONFIG_PATH"
fi
echo "  export LD_LIBRARY_PATH=$INSTALL_DIR/lib:\$LD_LIBRARY_PATH"

echo -n "  ./autogen.sh && ./configure --prefix=$INSTALL_DIR "
echo "CPPFLAGS=-I$INSTALL_DIR/include LDFLAGS=-L$INSTALL_DIR/lib"
echo "  make && make install"
echo ""
echo "*************************************************************************"
