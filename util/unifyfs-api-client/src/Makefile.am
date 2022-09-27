libexec_PROGRAMS = \
  unifyfs-laminate \
  unifyfs-remove

bin_PROGRAMS = \
  unifyfs-ls

CLEANFILES = $(libexec_PROGRAMS) $(bin_PROGRAMS)

# Compiler/linker flags

AM_CFLAGS = -Wall -Werror

api_client_cppflags = \
   $(AM_CPPFLAGS) \
   -I$(top_srcdir)/client/src \
   -I$(top_srcdir)/common/src

api_client_cxxflags = \
   $(AM_CXXFLAGS) \
   -std=c++11

api_client_ldadd   = $(top_builddir)/client/src/libunifyfs_api.la
api_client_ldflags = $(AM_LDFLAGS) -static

# Per-target flags begin here

unifyfs_laminate_CPPFLAGS = $(api_client_cppflags)
unifyfs_laminate_LDADD    = $(api_client_ldadd)
unifyfs_laminate_LDFLAGS  = $(api_client_ldflags)
unifyfs_laminate_SOURCES  = unifyfs-laminate.c

unifyfs_ls_CPPFLAGS = $(api_client_cppflags)
unifyfs_ls_CXXFLAGS = $(api_client_cxxflags)
unifyfs_ls_LDADD    = $(api_client_ldadd)
unifyfs_ls_LDFLAGS  = $(api_client_ldflags)
unifyfs_ls_SOURCES  = unifyfs-ls.cpp

unifyfs_remove_CPPFLAGS = $(api_client_cppflags)
unifyfs_remove_LDADD    = $(api_client_ldadd)
unifyfs_remove_LDFLAGS  = $(api_client_ldflags)
unifyfs_remove_SOURCES  = unifyfs-remove.c
