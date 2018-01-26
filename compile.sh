#!/bin/sh
gcc main.c																\
																		\
	./core/memory.c														\
	./core/buffer.c														\
	./core/utils.c														\
	./core/kv.c															\
	./core/config.c														\
	./core/ssl.c														\
	./core/socket.c														\
	./core/server.c														\
	./core/jobinternal.c												\
	./core/connection.c													\
	./core/fdevent.c													\
	./core/request.c													\
	./core/response.c													\
	./core/threads.c													\
	./core/joblist.c													\
	./core/route.c														\
	./core/session.c													\
	./core/chunk.c														\
	./core/db.c															\
	./core/db_mysql.c													\
	./core/extensions.c													\
	./core/ajax.c														\
	./core/stree.c														\
	./core/darray.c														\
	./core/event.c														\
																		\
	./framework/user.c													\
	./framework/language.c												\
	./framework/template.c												\
																		\
	-o xgserver															\
																		\
	-I/usr/local/include/												\
	-I./core/															\
	-I./framework/														\
	-L/usr/lib/															\
	-lm -lpthread -lz -lssl -lcrypto -lmysqlclient 						\
	-rdynamic -ldl \
	-Wall -ggdb -g3 -O0
