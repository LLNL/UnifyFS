testutil_headers = \
  testutil.h \
  testutil_rdwr.h

noinst_HEADERS = \
  $(testutil_headers) \
  testlib.h

libexec_PROGRAMS = \
  cr-posix \
  read-posix \
  write-posix \
  writeread-posix

if HAVE_LD_WRAP
libexec_PROGRAMS += \
  app-btio-static \
  app-mpiio-static \
  app-tileio-static \
  chmod-static \
  cr-static \
  multi-write-static \
  read-static \
  read-data-static \
  simul-static \
  size-static \
  transfer-static \
  write-static \
  writeread-static \
  write-transfer-static
endif #HAVE_LD_WRAP

if HAVE_GOTCHA

libexec_PROGRAMS += \
  app-btio-gotcha \
  app-mpiio-gotcha \
  app-tileio-gotcha \
  chmod-gotcha \
  cr-gotcha \
  multi-write-gotcha \
  read-gotcha \
  read-data-gotcha \
  simul-gotcha \
  size-gotcha \
  transfer-gotcha \
  write-gotcha \
  writeread-gotcha \
  write-transfer-gotcha

if HAVE_HDF5
libexec_PROGRAMS += \
  app-hdf5-create-gotcha \
  app-hdf5-writeread-gotcha
endif #HAVE_HDF5

if HAVE_FORTRAN
libexec_PROGRAMS += \
  writeread-ftn
endif #HAVE_FORTRAN

endif #HAVE_GOTCHA

CLEANFILES = $(libexec_PROGRAMS)


# Common compile/link flag definitions

ex_includes = -I$(top_srcdir)/client/src -I$(top_srcdir)/common/src

ex_cppflags     = $(AM_CPPFLAGS) $(ex_includes)
ex_mpi_cppflags = $(ex_cppflags) $(MPI_CFLAGS)

if USE_PMPI_WRAPPERS
ex_mpi_cppflags += -DENABLE_MPI_MOUNT
ex_gotcha_lib = \
  $(top_builddir)/client/src/libunifyfs_mpi_gotcha.la \
  $(top_builddir)/client/src/libunifyfs_gotcha.la
ex_static_lib = \
  $(top_builddir)/client/src/libunifyfs_mpi.la \
  $(top_builddir)/client/src/libunifyfs.la
else
ex_gotcha_lib = $(top_builddir)/client/src/libunifyfs_gotcha.la
ex_static_lib = $(top_builddir)/client/src/libunifyfs.la
endif #USE_PMPI_WRAPPERS

if HAVE_FORTRAN

ex_ftn_flags     = $(AM_FCFLAGS) $(ex_includes)
ex_ftn_mpi_flags = $(ex_ftn_flags) $(MPI_FFLAGS)

ex_ftn_ldadd = \
  $(top_builddir)/client/src/libunifyfsf.la \
  -lrt -lm \
  $(FCLIBS)

ex_ftn_mpi_ldadd = \
  $(ex_ftn_ldadd) \
  $(MPI_FLDFLAGS)

endif #HAVE_FORTRAN

if HAVE_HDF5
ex_hdf_ldadd = $(HDF5_LDFLAGS) $(HDF5_LIBS)
endif #HAVE_HDF5

ex_gotcha_ldadd     = $(ex_gotcha_lib) -lrt -lm
ex_gotcha_mpi_ldadd = $(ex_gotcha_ldadd) $(MPI_CLDFLAGS)

ex_posix_cppflags     = $(AM_CPPFLAGS) -DDISABLE_UNIFYFS
ex_posix_mpi_cppflags = $(ex_posix_cppflags) $(MPI_CFLAGS)

ex_posix_ldadd     = -lrt -lm
ex_posix_mpi_ldadd = $(ex_posix_ldadd) $(MPI_CLDFLAGS)

ex_static_ldadd     = $(ex_static_lib) -lrt -lm
ex_static_mpi_ldadd = $(ex_static_ldadd) $(MPI_CLDFLAGS)
ex_static_ldflags   = $(AM_LDFLAGS) $(LINK_WRAPPERS) -static

# Per-target flags begin here

app_btio_gotcha_SOURCES  = app-btio.c
app_btio_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
app_btio_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

app_btio_static_SOURCES  = app-btio.c
app_btio_static_CPPFLAGS = $(ex_mpi_cppflags)
app_btio_static_LDADD    = $(ex_static_mpi_ldadd)
app_btio_static_LDFLAGS  = $(ex_static_ldflags)

app_hdf5_create_gotcha_SOURCES  = app-hdf5-create.c
app_hdf5_create_gotcha_CPPFLAGS = $(ex_mpi_cppflags) $(HDF5_CPPFLAGS)
app_hdf5_create_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd) $(ex_hdf_ldadd)

app_hdf5_writeread_gotcha_SOURCES  = app-hdf5-writeread.c
app_hdf5_writeread_gotcha_CPPFLAGS = $(ex_mpi_cppflags) $(HDF5_CPPFLAGS)
app_hdf5_writeread_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd) $(ex_hdf_ldadd)

app_mpiio_gotcha_SOURCES  = app-mpiio.c
app_mpiio_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
app_mpiio_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

app_mpiio_static_SOURCES  = app-mpiio.c
app_mpiio_static_CPPFLAGS = $(ex_mpi_cppflags)
app_mpiio_static_LDADD    = $(ex_static_mpi_ldadd)
app_mpiio_static_LDFLAGS  = $(ex_static_ldflags)

app_tileio_gotcha_SOURCES  = app-tileio.c
app_tileio_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
app_tileio_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

app_tileio_static_SOURCES  = app-tileio.c
app_tileio_static_CPPFLAGS = $(ex_mpi_cppflags)
app_tileio_static_LDADD    = $(ex_static_mpi_ldadd)
app_tileio_static_LDFLAGS  = $(ex_static_ldflags)

chmod_gotcha_SOURCES  = chmod.c testutil.c $(testutil_headers)
chmod_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
chmod_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

chmod_static_SOURCES  = chmod.c testutil.c $(testutil_headers)
chmod_static_CPPFLAGS = $(ex_mpi_cppflags)
chmod_static_LDADD    = $(ex_static_mpi_ldadd)
chmod_static_LDFLAGS  = $(ex_static_ldflags)

cr_posix_SOURCES  = checkpoint-restart.c $(testutil_headers)
cr_posix_CPPFLAGS = $(ex_posix_mpi_cppflags)
cr_posix_LDADD    = $(ex_posix_mpi_ldadd)

cr_gotcha_SOURCES  = checkpoint-restart.c $(testutil_headers)
cr_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
cr_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

cr_static_SOURCES  = checkpoint-restart.c $(testutil_headers)
cr_static_CPPFLAGS = $(ex_mpi_cppflags)
cr_static_LDADD    = $(ex_static_mpi_ldadd)
cr_static_LDFLAGS  = $(ex_static_ldflags)

multi_write_gotcha_SOURCES  = multi-write.c testutil.c $(testutil_headers)
multi_write_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
multi_write_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

multi_write_static_SOURCES  = multi-write.c testutil.c $(testutil_headers)
multi_write_static_CPPFLAGS = $(ex_mpi_cppflags)
multi_write_static_LDADD    = $(ex_static_mpi_ldadd)
multi_write_static_LDFLAGS  = $(ex_static_ldflags)

read_posix_SOURCES  = read.c $(testutil_headers)
read_posix_CPPFLAGS = $(ex_posix_mpi_cppflags)
read_posix_LDADD    = $(ex_posix_mpi_ldadd)

read_gotcha_SOURCES  = read.c $(testutil_headers)
read_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
read_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

read_static_SOURCES  = read.c $(testutil_headers)
read_static_CPPFLAGS = $(ex_mpi_cppflags)
read_static_LDADD    = $(ex_static_mpi_ldadd)
read_static_LDFLAGS  = $(ex_static_ldflags)

read_data_gotcha_SOURCES  = read-data.c $(testutil_headers)
read_data_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
read_data_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

read_data_static_SOURCES  = read-data.c $(testutil_headers)
read_data_static_CPPFLAGS = $(ex_mpi_cppflags)
read_data_static_LDADD    = $(ex_static_mpi_ldadd)
read_data_static_LDFLAGS  = $(ex_static_ldflags)

simul_gotcha_SOURCES  = simul.c
simul_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
simul_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

simul_static_SOURCES  = simul.c
simul_static_CPPFLAGS = $(ex_mpi_cppflags)
simul_static_LDADD    = $(ex_static_mpi_ldadd)
simul_static_LDFLAGS  = $(ex_static_ldflags)

size_gotcha_SOURCES  = size.c testutil.c $(testutil_headers)
size_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
size_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

size_static_SOURCES  = size.c testutil.c $(testutil_headers)
size_static_CPPFLAGS = $(ex_mpi_cppflags)
size_static_LDADD    = $(ex_static_mpi_ldadd)
size_static_LDFLAGS  = $(ex_static_ldflags)

transfer_gotcha_SOURCES  = transfer.c
transfer_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
transfer_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

transfer_static_SOURCES  = transfer.c
transfer_static_CPPFLAGS = $(ex_mpi_cppflags)
transfer_static_LDADD    = $(ex_static_mpi_ldadd)
transfer_static_LDFLAGS  = $(ex_static_ldflags)

write_posix_SOURCES  = write.c $(testutil_headers)
write_posix_CPPFLAGS = $(ex_posix_mpi_cppflags)
write_posix_LDADD    = $(ex_posix_mpi_ldadd)

write_gotcha_SOURCES  = write.c $(testutil_headers)
write_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
write_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

write_static_SOURCES  = write.c $(testutil_headers)
write_static_CPPFLAGS = $(ex_mpi_cppflags)
write_static_LDADD    = $(ex_static_mpi_ldadd)
write_static_LDFLAGS  = $(ex_static_ldflags)

writeread_posix_SOURCES  = writeread.c testutil.c $(testutil_headers)
writeread_posix_CPPFLAGS = $(ex_posix_mpi_cppflags)
writeread_posix_LDADD    = $(ex_posix_mpi_ldadd)

writeread_ftn_SOURCES = writeread.f90
writeread_ftn_FCFLAGS = $(ex_ftn_mpi_flags)
writeread_ftn_LDADD   = $(ex_ftn_mpi_ldadd)

writeread_gotcha_SOURCES  = writeread.c testutil.c $(testutil_headers)
writeread_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
writeread_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

writeread_static_SOURCES  = writeread.c testutil.c $(testutil_headers)
writeread_static_CPPFLAGS = $(ex_mpi_cppflags)
writeread_static_LDADD    = $(ex_static_mpi_ldadd)
writeread_static_LDFLAGS  = $(ex_static_ldflags)

write_transfer_gotcha_SOURCES  = write-transfer.c testutil.c $(testutil_headers)
write_transfer_gotcha_CPPFLAGS = $(ex_mpi_cppflags)
write_transfer_gotcha_LDADD    = $(ex_gotcha_mpi_ldadd)

write_transfer_static_SOURCES  = write-transfer.c testutil.c $(testutil_headers)
write_transfer_static_CPPFLAGS = $(ex_mpi_cppflags)
write_transfer_static_LDADD    = $(ex_static_mpi_ldadd)
write_transfer_static_LDFLAGS  = $(ex_static_ldflags)
