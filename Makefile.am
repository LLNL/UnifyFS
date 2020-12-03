SUBDIRS = common meta server client examples extras t util

CONFIG = ordered

ACLOCAL_AMFLAGS = -I m4

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = client/unifyfs.pc

CLEANFILES =

checkstyle:
	scripts/checkpatch.sh
