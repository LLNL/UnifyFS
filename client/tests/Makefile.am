libexec_PROGRAMS = test_write_gotcha test_read_gotcha test_write_static test_read_static

test_gotcha_ldadd = ../src/libunifycr_gotcha.la
test_static_ldadd = ../src/libunifycr.la
test_static_ldflags = -static $(CP_WRAPPERS) $(AM_LDFLAGS)
test_cppflags = $(AM_CPPFLAGS) -I$(top_srcdir)/client/src

test_write_gotcha_SOURCES = test_write.c
test_write_gotcha_CPPFLAGS = $(test_cppflags)
test_write_gotcha_LDADD   = $(test_gotcha_ldadd)
test_write_gotcha_LDFLAGS = $(AM_LDFLAGS)

test_read_gotcha_SOURCES = test_read.c
test_read_gotcha_CPPFLAGS = $(test_cppflags)
test_read_gotcha_LDADD   = $(test_gotcha_ldadd)
test_read_gotcha_LDFLAGS = $(AM_LDFLAGS)

test_write_static_SOURCES = test_write.c
test_write_static_CPPFLAGS = $(test_cppflags)
test_write_static_LDADD   = $(test_static_ldadd)
test_write_static_LDFLAGS = $(test_static_ldflags)

test_read_static_SOURCES = test_read.c
test_read_static_CPPFLAGS = $(test_cppflags)
test_read_static_LDADD   = $(test_static_ldadd)
test_read_static_LDFLAGS = $(test_static_ldflags)

