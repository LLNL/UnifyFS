noinst_PROGRAMS = sysio-write-gotcha sysio-write-static \
                  sysio-read-gotcha sysio-read-static \
                  sysio-writeread-gotcha sysio-writeread-static sysio-writeread-posix \
                  sysio-writeread2-gotcha sysio-writeread2-static \
                  sysio-dir-gotcha sysio-dir-static \
                  sysio-stat-gotcha sysio-stat-static \
                  app-mpiio-gotcha app-mpiio-static \
                  app-btio-gotcha app-btio-static \
                  app-tileio-gotcha app-tileio-static

noinst_HEADERS = testlib.h

test_gotcha_ldadd = $(top_builddir)/client/src/libunifycr_gotcha.la -lrt -lm $(MPI_CLDFLAGS)
test_static_ldadd = $(top_builddir)/client/src/libunifycr.la -lrt -lm
test_static_ldflags = -static $(CP_WRAPPERS) $(AM_LDFLAGS) $(MPI_CLDFLAGS)
test_cppflags = $(AM_CPPFLAGS) -I$(top_srcdir)/client/src $(MPI_CFLAGS)

sysio_write_gotcha_SOURCES = sysio-write.c
sysio_write_gotcha_CPPFLAGS = $(test_cppflags)
sysio_write_gotcha_LDADD = $(test_gotcha_ldadd)
sysio_write_gotcha_LDFLAGS = $(AM_LDFLAGS)

sysio_write_static_SOURCES = sysio-write.c
sysio_write_static_CPPFLAGS = $(test_cppflags)
sysio_write_static_LDADD   = $(test_static_ldadd)
sysio_write_static_LDFLAGS = $(test_static_ldflags)

sysio_read_gotcha_SOURCES = sysio-read.c
sysio_read_gotcha_CPPFLAGS = $(test_cppflags)
sysio_read_gotcha_LDADD = $(test_gotcha_ldadd)
sysio_read_gotcha_LDFLAGS = $(AM_LDFLAGS)

sysio_read_static_SOURCES = sysio-read.c
sysio_read_static_CPPFLAGS = $(test_cppflags)
sysio_read_static_LDADD   = $(test_static_ldadd)
sysio_read_static_LDFLAGS = $(test_static_ldflags)

sysio_writeread_posix_SOURCES = sysio-writeread.c
sysio_writeread_posix_CPPFLAGS = $(AM_CPPFLAGS) -DNO_UNIFYCR $(MPI_CFLAGS)
sysio_writeread_posix_LDADD = -lrt -lm
sysio_writeread_posix_LDFLAGS = $(AM_LDFLAGS) $(MPI_CLDFLAGS)

sysio_writeread_gotcha_SOURCES = sysio-writeread.c
sysio_writeread_gotcha_CPPFLAGS = $(test_cppflags)
sysio_writeread_gotcha_LDADD = $(test_gotcha_ldadd)
sysio_writeread_gotcha_LDFLAGS = $(AM_LDFLAGS)

sysio_writeread_static_SOURCES = sysio-writeread.c
sysio_writeread_static_CPPFLAGS = $(test_cppflags)
sysio_writeread_static_LDADD   = $(test_static_ldadd)
sysio_writeread_static_LDFLAGS = $(test_static_ldflags)

sysio_writeread2_gotcha_SOURCES = sysio-writeread2.c
sysio_writeread2_gotcha_CPPFLAGS = $(test_cppflags)
sysio_writeread2_gotcha_LDADD = $(test_gotcha_ldadd)
sysio_writeread2_gotcha_LDFLAGS = $(AM_LDFLAGS)

sysio_writeread2_static_SOURCES = sysio-writeread2.c
sysio_writeread2_static_CPPFLAGS = $(test_cppflags)
sysio_writeread2_static_LDADD   = $(test_static_ldadd)
sysio_writeread2_static_LDFLAGS = $(test_static_ldflags)

sysio_dir_gotcha_SOURCES = sysio-dir.c
sysio_dir_gotcha_CPPFLAGS = $(test_cppflags)
sysio_dir_gotcha_LDADD = $(test_gotcha_ldadd)
sysio_dir_gotcha_LDFLAGS = $(AM_LDFLAGS)

sysio_dir_static_SOURCES = sysio-dir.c
sysio_dir_static_CPPFLAGS = $(test_cppflags)
sysio_dir_static_LDADD   = $(test_static_ldadd)
sysio_dir_static_LDFLAGS = $(test_static_ldflags)

sysio_stat_gotcha_SOURCES = sysio-stat.c
sysio_stat_gotcha_CPPFLAGS = $(test_cppflags)
sysio_stat_gotcha_LDADD = $(test_gotcha_ldadd)
sysio_stat_gotcha_LDFLAGS = $(AM_LDFLAGS)

sysio_stat_static_SOURCES = sysio-stat.c
sysio_stat_static_CPPFLAGS = $(test_cppflags)
sysio_stat_static_LDADD   = $(test_static_ldadd)
sysio_stat_static_LDFLAGS = $(test_static_ldflags)

app_mpiio_gotcha_SOURCES = app-mpiio.c
app_mpiio_gotcha_CPPFLAGS = $(test_cppflags)
app_mpiio_gotcha_LDADD   = $(test_gotcha_ldadd)
app_mpiio_gotcha_LDFLAGS = $(AM_LDFLAGS)

app_mpiio_static_SOURCES = app-mpiio.c
app_mpiio_static_CPPFLAGS = $(test_cppflags)
app_mpiio_static_LDADD   = $(test_static_ldadd)
app_mpiio_static_LDFLAGS = $(test_static_ldflags)

app_btio_gotcha_SOURCES = app-btio.c
app_btio_gotcha_CPPFLAGS = $(test_cppflags)
app_btio_gotcha_LDADD   = $(test_gotcha_ldadd)
app_btio_gotcha_LDFLAGS = $(AM_LDFLAGS)

app_btio_static_SOURCES = app-btio.c
app_btio_static_CPPFLAGS = $(test_cppflags)
app_btio_static_LDADD   = $(test_static_ldadd)
app_btio_static_LDFLAGS = $(test_static_ldflags)

app_tileio_gotcha_SOURCES = app-tileio.c
app_tileio_gotcha_CPPFLAGS = $(test_cppflags)
app_tileio_gotcha_LDADD   = $(test_gotcha_ldadd)
app_tileio_gotcha_LDFLAGS = $(AM_LDFLAGS)

app_tileio_static_SOURCES = app-tileio.c
app_tileio_static_CPPFLAGS = $(test_cppflags)
app_tileio_static_LDADD   = $(test_static_ldadd)
app_tileio_static_LDFLAGS = $(test_static_ldflags)

if HAVE_HDF5
noinst_PROGRAMS += app-hdf5-create-gotcha \
                   app-hdf5-writeread-gotcha

app_hdf5_create_gotcha_SOURCES = app-hdf5-create.c
app_hdf5_create_gotcha_CPPFLAGS = $(test_cppflags) $(HDF5_CPPFLAGS)
app_hdf5_create_gotcha_LDADD   = $(test_gotcha_ldadd) $(HDF5_LIBS)
app_hdf5_create_gotcha_LDFLAGS = $(AM_LDFLAGS) $(HDF5_LDFLAGS)

app_hdf5_writeread_gotcha_SOURCES = app-hdf5-writeread.c
app_hdf5_writeread_gotcha_CPPFLAGS = $(test_cppflags) $(HDF5_CPPFLAGS)
app_hdf5_writeread_gotcha_LDADD   = $(test_gotcha_ldadd) $(HDF5_LIBS)
app_hdf5_writeread_gotcha_LDFLAGS = $(AM_LDFLAGS) $(HDF5_LDFLAGS)
endif

CLEANFILES = $(noinst_PROGRAMS)
