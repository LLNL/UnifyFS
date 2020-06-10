SUBDIRS = lib

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/t/tap-driver.sh

TESTS = \
	0001-setup.t \
	0100-sysio-gotcha.t \
	0200-stdio-gotcha.t \
	0500-sysio-static.t \
	0600-stdio-static.t \
	0700-unifyfs-stage-full.t \
	9005-unifyfs-unmount.t \
	9010-stop-unifyfsd.t \
	9020-mountpoint-empty.t \
	9100-metadata-api.t \
	9200-seg-tree-test.t \
	9201-slotmap-test.t \
	9300-unifyfs-stage-isolated.t \
	9999-cleanup.t

check_SCRIPTS = \
	0001-setup.t \
	0100-sysio-gotcha.t \
	0200-stdio-gotcha.t \
	0500-sysio-static.t \
	0600-stdio-static.t \
	0700-unifyfs-stage-full.t \
	9005-unifyfs-unmount.t \
	9010-stop-unifyfsd.t \
	9020-mountpoint-empty.t \
	9100-metadata-api.t \
	9200-seg-tree-test.t \
	9201-slotmap-test.t \
	9300-unifyfs-stage-isolated.t \
	9999-cleanup.t

EXTRA_DIST = \
	$(check_SCRIPTS) \
	sharness.d \
	sharness.sh \
	tap-driver.sh

AM_CFLAGS = -Wall -Werror

clean-local:
	rm -fr trash-directory.* test-results *.log test_run_env.sh

libexec_PROGRAMS = \
	common/seg_tree_test.t \
	common/slotmap_test.t \
	server/metadata.t \
	std/stdio-gotcha.t \
	std/stdio-static.t \
	sys/sysio-gotcha.t \
	sys/sysio-static.t \
	unifyfs_unmount.t


test_common_ldadd = \
	$(top_builddir)/t/lib/libtap.la \
	$(top_builddir)/t/lib/libtestutil.la \
	$(top_builddir)/common/src/libunifyfs_common.la

test_common_ldflags = \
    -static $(AM_LDFLAGS)

test_ldadd = \
	$(top_builddir)/t/lib/libtap.la \
	$(top_builddir)/t/lib/libtestutil.la \
	$(top_builddir)/client/src/libunifyfs_gotcha.la \
    $(MPI_CLDFLAGS) \
	$(FLATCC_LDFLAGS) $(FLATCC_LIBS)

test_static_ldadd = \
	$(top_builddir)/t/lib/libtap.la \
	$(top_builddir)/t/lib/libtestutil.la \
	$(top_builddir)/client/src/libunifyfs.la

test_static_ldflags = \
	-static $(AM_LDFLAGS) \
	$(CP_WRAPPERS) \
	$(MPI_CLDFLAGS) \
	$(FLATCC_LDFLAGS) $(FLATCC_LIBS)

test_metadata_ldadd = \
	$(top_builddir)/t/lib/libtap.la \
	$(top_builddir)/t/lib/libtestutil.la \
	$(top_builddir)/common/src/libunifyfs_common.la \
	$(top_builddir)/server/src/libunifyfsd.a \
	$(top_builddir)/meta/src/libmdhim.a \
	$(LEVELDB_LDFLAGS) $(LEVELDB_LIBS) \
	$(MPI_CLDFLAGS) \
	$(MERCURY_LDFLAGS) $(MERCURY_LIBS) \
	$(ARGOBOTS_LDFLAGS) $(ARGOBOTS_LIBS) \
	$(MARGO_LDFLAGS) $(MARGO_LIBS) \
	$(FLATCC_LDFLAGS) $(FLATCC_LIBS) \
	-lpthread -lm -lstdc++ -lrt

test_common_cppflags = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/common/src \
	-D_GNU_SOURCE \
	$(AM_CPPFLAGS)

test_meta_cppflags = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/server/src \
	-I$(top_srcdir)/common/src \
	-D_GNU_SOURCE \
	$(AM_CPPFLAGS) \
	$(MARGO_CFLAGS) \
	$(MPI_CFLAGS)

test_cppflags = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/client/src \
	-I$(top_srcdir)/common/src \
	-D_GNU_SOURCE \
	$(AM_CPPFLAGS) \
	$(MPI_CFLAGS)


sys_sysio_gotcha_t_SOURCES = sys/sysio_suite.h \
                             sys/sysio_suite.c \
                             sys/creat-close.c \
                             sys/creat64.c \
                             sys/mkdir-rmdir.c \
                             sys/open.c \
                             sys/open64.c \
                             sys/lseek.c \
                             sys/write-read.c \
                             sys/write-read-hole.c \
                             sys/truncate.c \
                             sys/unlink.c \
                             sys/chdir.c

sys_sysio_gotcha_t_CPPFLAGS = $(test_cppflags)
sys_sysio_gotcha_t_LDADD = $(test_ldadd)
sys_sysio_gotcha_t_LDFLAGS = $(AM_LDFLAGS)

sys_sysio_static_t_SOURCES = sys/sysio_suite.h \
                             sys/sysio_suite.c \
                             sys/creat-close.c \
                             sys/creat64.c \
                             sys/mkdir-rmdir.c \
                             sys/open.c \
                             sys/open64.c \
                             sys/lseek.c \
                             sys/write-read.c \
                             sys/write-read-hole.c \
                             sys/truncate.c \
                             sys/unlink.c \
                             sys/chdir.c

sys_sysio_static_t_CPPFLAGS = $(test_cppflags)
sys_sysio_static_t_LDADD = $(test_static_ldadd)
sys_sysio_static_t_LDFLAGS = $(test_static_ldflags)

std_stdio_gotcha_t_SOURCES = std/stdio_suite.h \
                             std/stdio_suite.c \
                             std/fopen-fclose.c \
                             std/fseek-ftell.c \
                             std/fwrite-fread.c \
                             std/fflush.c \
                             std/size.c

std_stdio_gotcha_t_CPPFLAGS = $(test_cppflags)
std_stdio_gotcha_t_LDADD = $(test_ldadd)
std_stdio_gotcha_t_LDFLAGS = $(AM_LDFLAGS)

std_stdio_static_t_SOURCES = std/stdio_suite.h \
                             std/stdio_suite.c \
                             std/fopen-fclose.c \
                             std/fseek-ftell.c \
                             std/fwrite-fread.c \
                             std/fflush.c \
                             std/size.c

std_stdio_static_t_CPPFLAGS = $(test_cppflags)
std_stdio_static_t_LDADD = $(test_static_ldadd)
std_stdio_static_t_LDFLAGS = $(test_static_ldflags)

server_metadata_t_SOURCES = \
	server/metadata_suite.h \
	server/metadata_suite.c \
	server/unifyfs_meta_get_test.c

server_metadata_t_CPPFLAGS = $(test_meta_cppflags)
server_metadata_t_LDADD = $(test_metadata_ldadd)
server_metadata_t_LDFLAGS = $(AM_LDFLAGS)

unifyfs_unmount_t_SOURCES = unifyfs_unmount.c
unifyfs_unmount_t_CPPFLAGS = $(test_cppflags)
unifyfs_unmount_t_LDADD = $(test_static_ldadd)
unifyfs_unmount_t_LDFLAGS = $(test_static_ldflags)

common_seg_tree_test_t_SOURCES = common/seg_tree_test.c
common_seg_tree_test_t_CPPFLAGS = $(test_common_cppflags)
common_seg_tree_test_t_LDADD = $(test_common_ldadd)
common_seg_tree_test_t_LDFLAGS = $(test_common_ldflags)

common_slotmap_test_t_SOURCES = common/slotmap_test.c
common_slotmap_test_t_CPPFLAGS = $(test_common_cppflags)
common_slotmap_test_t_LDADD = $(test_common_ldadd)
common_slotmap_test_t_LDFLAGS = $(test_common_ldflags)
