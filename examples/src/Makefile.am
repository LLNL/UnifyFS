libexec_PROGRAMS = \
  cr-posix cr-gotcha cr-static \
  read-posix read-gotcha read-static \
  write-posix write-gotcha write-static \
  writeread-posix writeread-gotcha writeread-static writeread-ftn \
  sysio-write-gotcha sysio-write-static \
  sysio-read-gotcha sysio-read-static \
  sysio-writeread-gotcha sysio-writeread-static sysio-writeread-posix \
  sysio-writeread2-gotcha sysio-writeread2-static \
  sysio-dir-gotcha sysio-dir-static \
  sysio-stat-gotcha sysio-stat-static \
  sysio-cp-gotcha sysio-cp-static \
  app-mpiio-gotcha app-mpiio-static \
  app-btio-gotcha app-btio-static \
  app-tileio-gotcha app-tileio-static \
  transfer-gotcha transfer-static

if HAVE_HDF5
libexec_PROGRAMS += \
  app-hdf5-create-gotcha \
  app-hdf5-writeread-gotcha
endif

CLEANFILES = $(libexec_PROGRAMS)

noinst_HEADERS = \
  testlib.h \
  testutil.h \
  testutil_rdwr.h

# Common compile/link flag definitions

test_cppflags = $(AM_CPPFLAGS) $(MPI_CFLAGS) \
   -I$(top_srcdir)/client/src -I$(top_srcdir)/common/src

test_ftn_flags   = $(AM_FCFLAGS) $(MPI_FFLAGS) \
   -I$(top_srcdir)/client/src -I$(top_srcdir)/common/src
test_ftn_ldadd   = $(top_builddir)/client/src/libunifycrf.la -lrt -lm $(FCLIBS)
test_ftn_ldflags = $(AM_LDFLAGS) $(MPI_FLDFLAGS) \
    $(FLATCC_LDFLAGS) $(FLATCC_LIBS)

test_gotcha_ldadd   = $(top_builddir)/client/src/libunifycr_gotcha.la -lrt -lm
test_gotcha_ldflags = $(AM_LDFLAGS) $(MPI_CLDFLAGS) \
    $(FLATCC_LDFLAGS) $(FLATCC_LIBS)

test_posix_cppflags = $(AM_CPPFLAGS) $(MPI_CFLAGS) -DDISABLE_UNIFYCR
test_posix_ldadd    = -lrt -lm
test_posix_ldflags  = $(AM_LDFLAGS) $(MPI_CLDFLAGS)

test_static_ldadd   = $(top_builddir)/client/src/libunifycr.la -lrt -lm
test_static_ldflags = -static $(CP_WRAPPERS) $(AM_LDFLAGS) $(MPI_CLDFLAGS) \
    $(FLATCC_LDFLAGS) $(FLATCC_LIBS)

# Per-target flags begin here

sysio_write_gotcha_SOURCES  = sysio-write.c
sysio_write_gotcha_CPPFLAGS = $(test_cppflags)
sysio_write_gotcha_LDADD    = $(test_gotcha_ldadd)
sysio_write_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

sysio_write_static_SOURCES  = sysio-write.c
sysio_write_static_CPPFLAGS = $(test_cppflags)
sysio_write_static_LDADD    = $(test_static_ldadd)
sysio_write_static_LDFLAGS  = $(test_static_ldflags)

sysio_read_gotcha_SOURCES  = sysio-read.c
sysio_read_gotcha_CPPFLAGS = $(test_cppflags)
sysio_read_gotcha_LDADD    = $(test_gotcha_ldadd)
sysio_read_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

sysio_read_static_SOURCES  = sysio-read.c
sysio_read_static_CPPFLAGS = $(test_cppflags)
sysio_read_static_LDADD    = $(test_static_ldadd)
sysio_read_static_LDFLAGS  = $(test_static_ldflags)

sysio_writeread_posix_SOURCES  = sysio-writeread.c
sysio_writeread_posix_CPPFLAGS = $(test_posix_cppflags)
sysio_writeread_posix_LDADD    = $(test_posix_ldadd)
sysio_writeread_posix_LDFLAGS  = $(test_posix_ldflags)

sysio_writeread_gotcha_SOURCES  = sysio-writeread.c
sysio_writeread_gotcha_CPPFLAGS = $(test_cppflags)
sysio_writeread_gotcha_LDADD    = $(test_gotcha_ldadd)
sysio_writeread_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

sysio_writeread_static_SOURCES  = sysio-writeread.c
sysio_writeread_static_CPPFLAGS = $(test_cppflags)
sysio_writeread_static_LDADD    = $(test_static_ldadd)
sysio_writeread_static_LDFLAGS  = $(test_static_ldflags)

sysio_writeread2_gotcha_SOURCES  = sysio-writeread2.c
sysio_writeread2_gotcha_CPPFLAGS = $(test_cppflags)
sysio_writeread2_gotcha_LDADD    = $(test_gotcha_ldadd)
sysio_writeread2_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

sysio_writeread2_static_SOURCES  = sysio-writeread2.c
sysio_writeread2_static_CPPFLAGS = $(test_cppflags)
sysio_writeread2_static_LDADD    = $(test_static_ldadd)
sysio_writeread2_static_LDFLAGS  = $(test_static_ldflags)

sysio_dir_gotcha_SOURCES  = sysio-dir.c
sysio_dir_gotcha_CPPFLAGS = $(test_cppflags)
sysio_dir_gotcha_LDADD    = $(test_gotcha_ldadd)
sysio_dir_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

sysio_dir_static_SOURCES  = sysio-dir.c
sysio_dir_static_CPPFLAGS = $(test_cppflags)
sysio_dir_static_LDADD    = $(test_static_ldadd)
sysio_dir_static_LDFLAGS  = $(test_static_ldflags)

sysio_stat_gotcha_SOURCES  = sysio-stat.c
sysio_stat_gotcha_CPPFLAGS = $(test_cppflags)
sysio_stat_gotcha_LDADD    = $(test_gotcha_ldadd)
sysio_stat_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

sysio_stat_static_SOURCES  = sysio-stat.c
sysio_stat_static_CPPFLAGS = $(test_cppflags)
sysio_stat_static_LDADD    = $(test_static_ldadd)
sysio_stat_static_LDFLAGS  = $(test_static_ldflags)

sysio_cp_gotcha_SOURCES  = sysio-cp.c
sysio_cp_gotcha_CPPFLAGS = $(test_cppflags)
sysio_cp_gotcha_LDADD    = $(test_gotcha_ldadd)
sysio_cp_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

sysio_cp_static_SOURCES  = sysio-cp.c
sysio_cp_static_CPPFLAGS = $(test_cppflags)
sysio_cp_static_LDADD    = $(test_static_ldadd)
sysio_cp_static_LDFLAGS  = $(test_static_ldflags)

cr_posix_SOURCES  = checkpoint-restart.c
cr_posix_CPPFLAGS = $(test_posix_cppflags)
cr_posix_LDADD    = $(test_posix_ldadd)
cr_posix_LDFLAGS  = $(test_posix_ldflags)

cr_gotcha_SOURCES  = checkpoint-restart.c
cr_gotcha_CPPFLAGS = $(test_cppflags)
cr_gotcha_LDADD    = $(test_gotcha_ldadd)
cr_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

cr_static_SOURCES  = checkpoint-restart.c
cr_static_CPPFLAGS = $(test_cppflags)
cr_static_LDADD    = $(test_static_ldadd)
cr_static_LDFLAGS  = $(test_static_ldflags)

read_posix_SOURCES  = read.c
read_posix_CPPFLAGS = $(test_posix_cppflags)
read_posix_LDADD    = $(test_posix_ldadd)
read_posix_LDFLAGS  = $(test_posix_ldflags)

read_gotcha_SOURCES  = read.c
read_gotcha_CPPFLAGS = $(test_cppflags)
read_gotcha_LDADD    = $(test_gotcha_ldadd)
read_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

read_static_SOURCES  = read.c
read_static_CPPFLAGS = $(test_cppflags)
read_static_LDADD    = $(test_static_ldadd)
read_static_LDFLAGS  = $(test_static_ldflags)

write_posix_SOURCES  = write.c
write_posix_CPPFLAGS = $(test_posix_cppflags)
write_posix_LDADD    = $(test_posix_ldadd)
write_posix_LDFLAGS  = $(test_posix_ldflags)

write_gotcha_SOURCES  = write.c
write_gotcha_CPPFLAGS = $(test_cppflags)
write_gotcha_LDADD    = $(test_gotcha_ldadd)
write_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

write_static_SOURCES  = write.c
write_static_CPPFLAGS = $(test_cppflags)
write_static_LDADD    = $(test_static_ldadd)
write_static_LDFLAGS  = $(test_static_ldflags)

writeread_posix_SOURCES  = writeread.c
writeread_posix_CPPFLAGS = $(test_posix_cppflags)
writeread_posix_LDADD    = $(test_posix_ldadd)
writeread_posix_LDFLAGS  = $(test_posix_ldflags)

writeread_gotcha_SOURCES  = writeread.c
writeread_gotcha_CPPFLAGS = $(test_cppflags)
writeread_gotcha_LDADD    = $(test_gotcha_ldadd)
writeread_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

writeread_static_SOURCES  = writeread.c
writeread_static_CPPFLAGS = $(test_cppflags)
writeread_static_LDADD    = $(test_static_ldadd)
writeread_static_LDFLAGS  = $(test_static_ldflags)

writeread_ftn_SOURCES  = writeread.f90
writeread_ftn_FCFLAGS  = $(test_ftn_flags)
writeread_ftn_LDADD    = $(test_ftn_ldadd)
writeread_ftn_LDFLAGS  = $(test_ftn_ldflags)

app_mpiio_gotcha_SOURCES  = app-mpiio.c
app_mpiio_gotcha_CPPFLAGS = $(test_cppflags)
app_mpiio_gotcha_LDADD    = $(test_gotcha_ldadd)
app_mpiio_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

app_mpiio_static_SOURCES  = app-mpiio.c
app_mpiio_static_CPPFLAGS = $(test_cppflags)
app_mpiio_static_LDADD    = $(test_static_ldadd)
app_mpiio_static_LDFLAGS  = $(test_static_ldflags)

app_btio_gotcha_SOURCES  = app-btio.c
app_btio_gotcha_CPPFLAGS = $(test_cppflags)
app_btio_gotcha_LDADD    = $(test_gotcha_ldadd)
app_btio_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

app_btio_static_SOURCES  = app-btio.c
app_btio_static_CPPFLAGS = $(test_cppflags)
app_btio_static_LDADD    = $(test_static_ldadd)
app_btio_static_LDFLAGS  = $(test_static_ldflags)

app_tileio_gotcha_SOURCES  = app-tileio.c
app_tileio_gotcha_CPPFLAGS = $(test_cppflags)
app_tileio_gotcha_LDADD    = $(test_gotcha_ldadd)
app_tileio_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

app_tileio_static_SOURCES  = app-tileio.c
app_tileio_static_CPPFLAGS = $(test_cppflags)
app_tileio_static_LDADD    = $(test_static_ldadd)
app_tileio_static_LDFLAGS  = $(test_static_ldflags)

if HAVE_HDF5

app_hdf5_create_gotcha_SOURCES  = app-hdf5-create.c
app_hdf5_create_gotcha_CPPFLAGS = $(test_cppflags) $(HDF5_CPPFLAGS)
app_hdf5_create_gotcha_LDADD    = $(test_gotcha_ldadd) $(HDF5_LIBS)
app_hdf5_create_gotcha_LDFLAGS  = $(test_gotcha_ldflags) $(HDF5_LDFLAGS)

app_hdf5_writeread_gotcha_SOURCES  = app-hdf5-writeread.c
app_hdf5_writeread_gotcha_CPPFLAGS = $(test_cppflags) $(HDF5_CPPFLAGS)
app_hdf5_writeread_gotcha_LDADD    = $(test_gotcha_ldadd) $(HDF5_LIBS)
app_hdf5_writeread_gotcha_LDFLAGS  = $(test_gotcha_ldflags) $(HDF5_LDFLAGS)

endif

transfer_gotcha_SOURCES  = transfer.c
transfer_gotcha_CPPFLAGS = $(test_cppflags)
transfer_gotcha_LDADD    = $(test_gotcha_ldadd)
transfer_gotcha_LDFLAGS  = $(test_gotcha_ldflags)

transfer_static_SOURCES  = transfer.c
transfer_static_CPPFLAGS = $(test_cppflags)
transfer_static_LDADD    = $(test_static_ldadd)
transfer_static_LDFLAGS  = $(test_static_ldflags)


