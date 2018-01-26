#!/bin/sh
gcc -fPIC -c sudoku.c puzzle.c											\
	-I/usr/local/include/												\
	-I../../core/														\
	-I../../framework/													\
	-L/usr/lib/															\
	-lm
gcc -shared -o sudoku.so sudoku.o puzzle.o
