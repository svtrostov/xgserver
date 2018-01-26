#!/bin/sh
gcc -fPIC -c index.c													\
	-I/usr/local/include/												\
	-I../../core/													\
	-I../../framework/												\
	-L/usr/lib/
#	-lm -Wall -ggdb -g3
gcc -shared -o index.so index.o 
