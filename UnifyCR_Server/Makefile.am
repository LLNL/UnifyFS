AUTOMAKE_OPTIONS = foreign
noinst_PROGRAMS=unifycrd
unifycrd_SOURCES = unifycr_init.c unifycr_init.h \
								 	 unifycr_cmd_handler.c unifycr_cmd_handler.h \
									 unifycr_request_manager.c	unifycr_request_manager.h \
									 unifycr_service_manager.c  unifycr_service_manager.h \
									 unifycr_metadata.c unifycr_metadata.h \
									 unifycr_debug.c unifycr_debug.h \
									 arraylist.c arraylist.h \
									 unifycr_const.c unifycr_const.h \
									 unifycr_sock.c unifycr_sock.h
unifycrd_LDADD = -lpthread -lm -lstdc++ -lrt
