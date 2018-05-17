SUBDIRS = common meta server client extras t util

CONFIG = ordered

#ACLOCAL_AMFLAGS = -I m4

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = client/unifycr.pc

CLEANFILES =

checkstyle:
	scripts/checkpatch.sh
