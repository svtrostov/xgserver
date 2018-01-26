/***********************************************************************
 * XGSERVER EXTENSION
 * sudoku.c
 * Расширение: обработка sudoku страницы
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/


#include "core.h"
#include "kv.h"
#include "server.h"
#include "globals.h"
#include "session.h"
#include "db.h"
#include "user.h"
#include "language.h"
#include "template.h"
#include "sudoku.h"


static result_e 
handleSudoku(connection_s * con){

	buffer_s * body = bufferCreate(0);

	sudoku_s * sudoku = sudokuCreate(SPUZZLE_9X9, 1);

	bufferAddStringFormat(
		body,
		"<h2>SUDOKU PUZZLE: sizeof(sudoku_s) = %d</h2>\r\n",
		(int64_t)sizeof(sudoku_s)
	);
	echoBuffer(con, body);


	sudokuFree(sudoku);

	return RESULT_OK;
}





/*
 * Стартовая функция расширения
 */
int 
start(extension_s * extension){
	DEBUG_MSG("Extension loaded: %s\n", extension->filename);
	routeAdd("/sudoku", handleSudoku);
	sudoku_s * sudoku = sudokuCreate(SPUZZLE_9X9, 1);
	sudokuFree(sudoku);
	return 0;
}//END: start




/***********************************************************************
 * Функции
 **********************************************************************/



