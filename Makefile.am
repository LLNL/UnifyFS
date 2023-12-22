SUBDIRS = extras client server util examples t

CONFIG = ordered

ACLOCAL_AMFLAGS = -I m4

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = \
  client/unifyfs.pc \
  client/unifyfs-api.pc \
  client/unifyfs-static.pc

CLEANFILES =

checkstyle:
	scripts/checkpatch.sh
