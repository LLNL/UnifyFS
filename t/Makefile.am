SUBDIRS = lib

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/t/tap-driver.sh

TESTS = \
	0001-setup.t \
	0100-sysio.t \
	9005-unifycr-unmount.t \
	9010-stop-unifycrd.t \
	9020-mountpoint-empty.t

check_SCRIPTS = \
	0001-setup.t \
	0100-sysio.t \
	9005-unifycr-unmount.t \
	9010-stop-unifycrd.t \
	9020-mountpoint-empty.t

EXTRA_DIST = \
	$(check_SCRIPTS) \
	sharness.d \
	sharness.sh \
	tap-driver.sh

AM_CFLAGS = -Wall

clean-local:
	rm -fr trash-directory.* test-results *.log test_run_env.sh

libexec_PROGRAMS = \
	sys/sysio.t \
	unifycr_unmount.t

test_ldadd = \
	$(top_builddir)/t/lib/libtap.la \
	$(top_builddir)/t/lib/libtestutil.la \
	$(top_builddir)/client/src/libunifycr_gotcha.la \
        $(MPI_CLDFLAGS) $(FLATBUFFERS_LDFLAGS) $(FLATBUFFERS_LIBS)

test_cppflags = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/client/src \
	-I$(top_srcdir)/common/src \
	-D_GNU_SOURCE \
	$(AM_CPPFLAGS) \
        $(MPI_CFLAGS)

sys_sysio_t_SOURCES = sys/sysio_suite.h \
					  sys/sysio_suite.c \
					  sys/creat-close.c \
					  sys/open.c
sys_sysio_t_CPPFLAGS = $(test_cppflags)
sys_sysio_t_LDADD = $(test_ldadd)
sys_sysio_t_LDFLAGS = $(AM_LDFLAGS)

unifycr_unmount_t_SOURCES = unifycr_unmount.c
unifycr_unmount_t_CPPFLAGS = $(test_cppflags)
unifycr_unmount_t_LDADD = $(test_ldadd)
unifycr_unmount_t_LDFLAGS = $(AM_LDFLAGS)
