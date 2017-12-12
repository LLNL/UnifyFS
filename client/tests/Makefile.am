libexec_PROGRAMS = test_write_gotcha test_read_gotcha test_write_static test_read_static

test_write_gotcha_SOURCES = test_write.c
test_write_gotcha_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/include
test_write_gotcha_LDADD   = ../src/libunifycr_gotcha.la
test_write_gotcha_LDFLAGS = $(AM_LDFLAGS)

test_read_gotcha_SOURCES = test_read.c
test_read_gotcha_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/include
test_read_gotcha_LDADD   = ../src/libunifycr_gotcha.la
test_read_gotcha_LDFLAGS = $(AM_LDFLAGS)

test_write_static_SOURCES = test_write.c
test_write_static_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/include
test_write_static_LDADD   = ../src/libunifycr.la
test_write_static_LDFLAGS = -static $(CP_WRAPPERS) $(AM_LDFLAGS)

test_read_static_SOURCES = test_read.c
test_read_static_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/include
test_read_static_LDADD   = ../src/libunifycr.la
test_read_static_LDFLAGS = -static $(CP_WRAPPERS) $(AM_LDFLAGS)

