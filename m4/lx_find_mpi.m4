##########################################################################
# Written by Todd Gamblin, tgamblin@llnl.gov.
#
# LX_FIND_MPI()
#  ------------------------------------------------------------------------
# This macro finds an MPI compiler and extracts includes and libraries from
# it for use in automake projects.  The script exports the following variables:
#
# AC_DEFINE variables:
#     HAVE_MPI         AC_DEFINE'd to 1 if we found MPI
#
# AC_SUBST variables:
#     MPICC            Name of MPI compiler
#     MPI_CFLAGS       Includes and defines for MPI C compilation
#     MPI_CLDFLAGS     Libraries and library paths for linking MPI C programs
#
#     MPICXX           Name of MPI C++ compiler
#     MPI_CXXFLAGS     Includes and defines for MPI C++ compilation
#     MPI_CXXLDFLAGS   Libraries and library paths for linking MPI C++ programs
#
#     MPIF77           Name of MPI Fortran 77 compiler
#     MPI_F77FLAGS     Includes and defines for MPI Fortran 77 compilation
#     MPI_F77LDFLAGS   Libraries and library paths for linking MPI Fortran 77 programs
#
#     MPIFC            Name of MPI Fortran compiler
#     MPI_FFLAGS       Includes and defines for MPI Fortran compilation
#     MPI_FLDFLAGS     Libraries and library paths for linking MPI Fortran programs
#
# Shell variables output by this macro:
#     have_C_mpi       'yes' if we found MPI for C, 'no' otherwise
#     have_CXX_mpi     'yes' if we found MPI for C++, 'no' otherwise
#     have_F77_mpi     'yes' if we found MPI for F77, 'no' otherwise
#     have_F_mpi       'yes' if we found MPI for Fortran, 'no' otherwise
#
##########################################################################
AC_DEFUN([LX_FIND_MPI],
[
     AC_LANG_CASE(
     [C], [
         AC_REQUIRE([AC_PROG_CC])
         if [[ ! -z "$MPICC" ]]; then
             LX_QUERY_MPI_COMPILER(MPICC, [$MPICC], C)
         else
             LX_QUERY_MPI_COMPILER(MPICC, [mpicc mpiicc mpixlc mpipgcc], C)
         fi
     ],
     [C++], [
         AC_REQUIRE([AC_PROG_CXX])
         if [[ ! -z "$MPICXX" ]]; then
             LX_QUERY_MPI_COMPILER(MPICXX, [$MPICXX], CXX)
         else
             LX_QUERY_MPI_COMPILER(MPICXX, [mpicxx mpiCC mpic++ mpig++ mpiicpc mpipgCC mpixlC], CXX)
         fi
     ],
     [F77], [
         AC_REQUIRE([AC_PROG_F77])
         if [[ ! -z "$MPIF77" ]]; then
             LX_QUERY_MPI_COMPILER(MPIF77, [$MPIF77], F77)
         else
             LX_QUERY_MPI_COMPILER(MPIF77, [mpif77 mpiifort mpixlf77 mpixlf77_r], F77)
         fi
     ],
     [Fortran], [
         AC_REQUIRE([AC_PROG_FC])
         if [[ ! -z "$MPIFC" ]]; then
             LX_QUERY_MPI_COMPILER(MPIFC, [$MPIFC], F)
         else
             mpi_default_fc="mpif95 mpif90 mpigfortran mpif2003"
             mpi_intel_fc="mpiifort"
             mpi_xl_fc="mpixlf95 mpixlf95_r mpixlf90 mpixlf90_r mpixlf2003 mpixlf2003_r"
             mpi_pg_fc="mpipgf95 mpipgf90"
             LX_QUERY_MPI_COMPILER(MPIFC, [$mpi_default_fc $mpi_intel_fc $mpi_xl_fc $mpi_pg_fc], F)
         fi
     ])
])


#
# LX_QUERY_MPI_COMPILER([compiler-var-name], [compiler-names], [output-var-prefix])
#  ------------------------------------------------------------------------
# AC_SUBST variables:
#     MPI_<prefix>FLAGS       Includes and defines for MPI compilation
#     MPI_<prefix>LDFLAGS     Libraries and library paths for linking MPI C programs
#     MPI_<prefix>RPATH       RPath parameters extracted from the MPI C compiler
#
# Shell variables output by this macro:
#     found_mpi_flags         'yes' if we were able to get flags, 'no' otherwise
#
AC_DEFUN([LX_QUERY_MPI_COMPILER],
[
     # Try to find a working MPI compiler from the supplied names
     AC_PATH_PROGS($1, [$2], [not-found])

     # Figure out what the compiler responds to to get it to show us the compile
     # and link lines.  After this part of the macro, we'll have a valid
     # lx_mpi_command_line
     echo -n "Checking whether $$1 responds to '-showme:compile'... "
     lx_mpi_compile_line=`$$1 -showme:compile 2>/dev/null`
     if [[ "$?" -eq 0 ]]; then
         echo yes
         lx_mpi_link_line=`$$1 -showme:link 2>/dev/null`
     else
         echo no
         echo -n "Checking whether $$1 responds to '-showme'... "
         lx_mpi_command_line=`$$1 -showme 2>/dev/null`
         if [[ "$?" -ne 0 ]]; then
             echo no
             echo -n "Checking whether $$1 responds to '-compile-info'... "
             lx_mpi_compile_line=`$$1 -compile-info 2>/dev/null`
             if [[ "$?" -eq 0 ]]; then
                 echo yes
                 lx_mpi_link_line=`$$1 -link-info 2>/dev/null`
             else
                 echo no
                 echo -n "Checking whether $$1 responds to '-show'... "
                 lx_mpi_command_line=`$$1 -show 2>/dev/null`
                 if [[ "$?" -eq 0 ]]; then
                     echo yes
                 else
                     echo no
                     echo -n "Checking whether $$1 responds to '--cray-print-opts=all'... "
                     lx_mpi_command_line=`$$1 --cray-print-opts=all 2>/dev/null`
                     if [[ "$?" -eq 0 ]]; then
                         echo yes
                     else
                         echo no
                     fi
                 fi
             fi
         else
             echo yes
         fi
     fi

     if [[ ! -z "$lx_mpi_compile_line" -a ! -z "$lx_mpi_link_line" ]]; then
         lx_mpi_command_line="$lx_mpi_compile_line $lx_mpi_link_line"
     fi

     if [[ ! -z "$lx_mpi_command_line" ]]; then
         # Now extract the different parts of the MPI command line.  Do these separately in case we need to
         # parse them all out in future versions of this macro.
         lx_mpi_defines=`    echo "$lx_mpi_command_line" | grep -o -- '\(^\| \)-D\([[^\"[:space:]]]\+\|\"[[^\"[:space:]]]\+\"\)'`
         lx_mpi_includes=`   echo "$lx_mpi_command_line" | grep -o -- '\(^\| \)-I\([[^\"[:space:]]]\+\|\"[[^\"[:space:]]]\+\"\)'`
         lx_mpi_link_paths=` echo "$lx_mpi_command_line" | grep -o -- '\(^\| \)-L\([[^\"[:space:]]]\+\|\"[[^\"[:space:]]]\+\"\)'`
         lx_mpi_libs=`       echo "$lx_mpi_command_line" | grep -o -- '\(^\| \)-l\([[^\"[:space:]]]\+\|\"[[^\"[:space:]]]\+\"\)'`
         lx_mpi_link_args=`  echo "$lx_mpi_command_line" | grep -o -- '\(^\| \)-Wl,\([[^\"[:space:]]]\+\|\"[[^\"[:space:]]]\+\"\)'`

         # Create variables and clean up newlines and multiple spaces
         MPI_$3FLAGS="$lx_mpi_defines $lx_mpi_includes"
         MPI_$3LDFLAGS="$lx_mpi_link_paths $lx_mpi_libs $lx_mpi_link_args"
         MPI_$3FLAGS=`  echo "$MPI_$3FLAGS"   | tr '\n' ' ' | sed 's/^[[ \t]]*//;s/[[ \t]]*$//' | sed 's/  */ /g'`
         MPI_$3LDFLAGS=`echo "$MPI_$3LDFLAGS" | tr '\n' ' ' | sed 's/^[[ \t]]*//;s/[[ \t]]*$//' | sed 's/  */ /g'`

         # Add a define for testing at compile time.
         AC_DEFINE([HAVE_MPI], [1], [Define to 1 if you have MPI libs and headers.])

         # AC_SUBST everything.
         AC_SUBST($1)
         AC_SUBST(MPI_$3FLAGS)
         AC_SUBST(MPI_$3LDFLAGS)

         # set a shell variable that the caller can test outside this macro
         have_$3_mpi='yes'
     else
         echo Unable to find suitable MPI Compiler. Try setting $1.
         have_$3_mpi='no'
     fi
])

