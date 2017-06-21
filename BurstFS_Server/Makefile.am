AUTOMAKE_OPTIONS = foreign
noinst_PROGRAMS=burstfsd
burstfsd_SOURCES = burstfs_init.c burstfs_init.h \
								 	 burstfs_cmd_handler.c burstfs_cmd_handler.h \
									 burstfs_request_manager.c	burstfs_request_manager.h \
									 burstfs_service_manager.c  burstfs_service_manager.h \
									 burstfs_metadata.c burstfs_metadata.h \
									 burstfs_debug.c burstfs_debug.h \
									 arraylist.c arraylist.h \
									 burstfs_const.c burstfs_const.h \
									 burstfs_sock.c burstfs_sock.h
burstfsd_LDADD = -lpthread -lm -lstdc++ -lrt
