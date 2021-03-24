SUBDIRS = lib

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/t/tap-driver.sh

# Order matters
TESTS = \
  0001-setup.t

if HAVE_GOTCHA
  TESTS += \
    0100-sysio-gotcha.t \
    0110-statfs-gotcha.t \
    0200-stdio-gotcha.t
endif

TESTS += \
  0500-sysio-static.t \
  0510-statfs-static.t \
  0600-stdio-static.t \
  0700-unifyfs-stage-full.t \
  9005-unifyfs-unmount.t \
  9010-stop-unifyfsd.t \
  9020-mountpoint-empty.t \
  9200-seg-tree-test.t \
  9201-slotmap-test.t \
  9300-unifyfs-stage-isolated.t \
  9999-cleanup.t

check_SCRIPTS = \
  0001-setup.t \
  0500-sysio-static.t \
  0510-statfs-static.t \
  0600-stdio-static.t \
  0700-unifyfs-stage-full.t \
  9005-unifyfs-unmount.t \
  9010-stop-unifyfsd.t \
  9020-mountpoint-empty.t \
  9200-seg-tree-test.t \
  9201-slotmap-test.t \
  9300-unifyfs-stage-isolated.t \
  9999-cleanup.t

if HAVE_GOTCHA
  check_SCRIPTS += \
    0100-sysio-gotcha.t \
    0110-statfs-gotcha.t \
    0200-stdio-gotcha.t
endif

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
  std/stdio-static.t \
  sys/statfs-static.t \
  sys/sysio-static.t \
  unifyfs_unmount.t

if HAVE_GOTCHA
  libexec_PROGRAMS += \
    std/stdio-gotcha.t \
    sys/statfs-gotcha.t \
    sys/sysio-gotcha.t
endif

test_common_ldadd = \
  $(top_builddir)/t/lib/libtap.la \
  $(top_builddir)/t/lib/libtestutil.la

test_common_ldflags = \
  $(AM_LDFLAGS) \
  -static -lpthread

test_gotcha_ldadd = \
  $(top_builddir)/t/lib/libtap.la \
  $(top_builddir)/t/lib/libtestutil.la \
  $(top_builddir)/client/src/libunifyfs_gotcha.la

test_gotcha_ldflags = \
  $(AM_LDFLAGS)\
  $(MPI_CLDFLAGS)

test_wrap_ldadd = \
  $(top_builddir)/t/lib/libtap.la \
  $(top_builddir)/t/lib/libtestutil.la \
  $(top_builddir)/client/src/libunifyfs.la

test_wrap_ldflags = \
  $(AM_LDFLAGS) \
  $(MPI_CLDFLAGS) \
  $(CP_WRAPPERS) \
  -static

test_common_cppflags = \
  -I$(top_srcdir) \
  -I$(top_srcdir)/common/src \
  -D_GNU_SOURCE \
  $(AM_CPPFLAGS)

test_cppflags = \
  -I$(top_srcdir) \
  -I$(top_srcdir)/client/src \
  -I$(top_srcdir)/common/src \
  -D_GNU_SOURCE \
  $(AM_CPPFLAGS) \
  $(MPI_CFLAGS)


sys_sysio_gotcha_t_SOURCES = \
  sys/sysio_suite.h \
  sys/sysio_suite.c \
  sys/statfs.c \
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
sys_sysio_gotcha_t_LDADD = $(test_gotcha_ldadd)
sys_sysio_gotcha_t_LDFLAGS = $(test_gotcha_ldflags)

sys_sysio_static_t_SOURCES = \
  sys/sysio_suite.h \
  sys/sysio_suite.c \
  sys/statfs.c \
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
sys_sysio_static_t_LDADD = $(test_wrap_ldadd)
sys_sysio_static_t_LDFLAGS = $(test_wrap_ldflags)

sys_statfs_gotcha_t_SOURCES = \
  sys/statfs_suite.h \
  sys/statfs_suite.c \
  sys/statfs.c

sys_statfs_gotcha_t_CPPFLAGS = $(test_cppflags)
sys_statfs_gotcha_t_LDADD = $(test_gotcha_ldadd)
sys_statfs_gotcha_t_LDFLAGS = $(test_gotcha_ldflags)

sys_statfs_static_t_SOURCES = \
  sys/statfs_suite.h \
  sys/statfs_suite.c \
  sys/statfs.c

sys_statfs_static_t_CPPFLAGS = $(test_cppflags)
sys_statfs_static_t_LDADD = $(test_wrap_ldadd)
sys_statfs_static_t_LDFLAGS = $(test_wrap_ldflags)

std_stdio_gotcha_t_SOURCES = \
  std/stdio_suite.h \
  std/stdio_suite.c \
  std/fopen-fclose.c \
  std/fseek-ftell.c \
  std/fwrite-fread.c \
  std/fflush.c \
  std/size.c

std_stdio_gotcha_t_CPPFLAGS = $(test_cppflags)
std_stdio_gotcha_t_LDADD = $(test_gotcha_ldadd)
std_stdio_gotcha_t_LDFLAGS = $(test_gotcha_ldflags)

std_stdio_static_t_SOURCES = \
  std/stdio_suite.h \
  std/stdio_suite.c \
  std/fopen-fclose.c \
  std/fseek-ftell.c \
  std/fwrite-fread.c \
  std/fflush.c \
  std/size.c

std_stdio_static_t_CPPFLAGS = $(test_cppflags)
std_stdio_static_t_LDADD = $(test_wrap_ldadd)
std_stdio_static_t_LDFLAGS = $(test_wrap_ldflags)

unifyfs_unmount_t_SOURCES = unifyfs_unmount.c
unifyfs_unmount_t_CPPFLAGS = $(test_cppflags)
unifyfs_unmount_t_LDADD = $(test_wrap_ldadd)
unifyfs_unmount_t_LDFLAGS = $(test_wrap_ldflags)

common_seg_tree_test_t_SOURCES = \
  common/seg_tree_test.c \
  ../common/src/seg_tree.c \
  ../common/src/unifyfs_log.c \
  ../common/src/unifyfs_misc.c
common_seg_tree_test_t_CPPFLAGS = $(test_common_cppflags)
common_seg_tree_test_t_LDADD = $(test_common_ldadd)
common_seg_tree_test_t_LDFLAGS = $(test_common_ldflags)

common_slotmap_test_t_SOURCES = \
  common/slotmap_test.c \
  ../common/src/slotmap.c
common_slotmap_test_t_CPPFLAGS = $(test_common_cppflags)
common_slotmap_test_t_LDADD = $(test_common_ldadd)
common_slotmap_test_t_LDFLAGS = $(test_common_ldflags)
