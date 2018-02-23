bin_PROGRAMS = unifycr

unifycr_SOURCES = unifycr.c \
                  unifycr-env.c \
                  unifycr-rm.c \
                  unifycr-sysconf.c \
                  unifycr-runstate.c \
		  toml.c

noinst_HEADERS = unifycr.h toml.h

#unifycr_LDADD = 

AM_CPPFLAGS = -DCONFDIR=\"$(sysconfdir)/unifycr\" \
              -DBINDIR=\"$(bindir)\" \
              -DSBINDIR=\"$(sbindir)\" \
              -DRUNDIR=\"$(runstatedir)/unifycr\"

AM_CFLAGS = -Wall

CLEANFILES = $(bin_PROGRAMS)

