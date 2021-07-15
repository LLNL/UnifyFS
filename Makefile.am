SUBDIRS = extras meta client server util examples t

CONFIG = ordered

ACLOCAL_AMFLAGS = -I m4

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = client/unifyfs.pc

CLEANFILES =

checkstyle:
	scripts/checkpatch.sh
