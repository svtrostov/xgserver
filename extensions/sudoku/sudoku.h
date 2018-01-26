/***********************************************************************
 * XG SERVER
 * core/darray.h
 * Динамический массив
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   



#ifndef _XGEXTSUDOKU_H
#define _XGEXTSUDOKU_H

#ifdef __cplusplus
extern "C" {
#endif


/***********************************************************************
 * Подключаемые заголовки
 **********************************************************************/
#include <limits.h>
#include "core.h"


#define SUDOKU_MAP "1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define SUDOKU_MAX_LINE 36
#define SUDOKU_MAX_SIDE 6


/***********************************************************************
 * Константы
 **********************************************************************/
 



/***********************************************************************
 * Объявления и декларации
 **********************************************************************/

//Типы SUDOKU
typedef enum{
	SPUZZLE_9X9 		= 0,	//Классический пазл 9х9 (123456789 в 1 слой)
	SPUZZLE_9X93D 		= 1,	//3D пазл 9х9х9  (123456789 в 9 слоев)
	SPUZZLE_16X16		= 2,	//Пазл 16х16  (0123456789ABCDEF в 1 слой)
	SPUZZLE_25X25		= 3,	//Пазл 25х25  (0123456789ABCDEFGHIJKLMNO в 1 слой)
	SPUZZLE_36X36 		= 4		//Расширенный пазл 36х36  (0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ в 1 слой)
} puzzle_t;




/***********************************************************************
 * Структуры
 **********************************************************************/
typedef struct		type_sudoku_s	sudoku_s;	//Структура Sudoku
typedef struct		type_bsector_s	bsector_s;
typedef struct		type_bcell_s	bcell_s;


//Ячейка доски
typedef struct type_bcell_s{
	uint8_t			x;			//X координата ячейки на доске
	uint8_t			y;			//Y координата ячейки на доске
	uint8_t			sector;		//ID сектора, в котором находится ячейка
	uint8_t			sx;			//X координата ячейки в секторе
	uint8_t			sy;			//Y координата ячейки в секторе
	u_char			value;		//Заданное значение
	u_char			mask;		//Первоначальное значение для этой ячейки
} bcell_s;

typedef struct type_bmask_s{
	uint8_t			x;			//X координата ячейки в секторе
	uint8_t			y;			//Y координата ячейки в секторе
}bmask_s;


//Структура символа игровой доски
typedef struct type_bsymbol_s{
	bcell_s			*sets[_SUDOKUMAPSIZE];						//Массив измененных ячеек доски (только указатели)
	uint8_t			count;										//Количество измененных ячеек
	u_char			ch;											//Символ
}bchar_s;



//Структура сектора доски
typedef struct type_bsector_s{
	bcell_s		cells[SUDOKU_MAX_SIDE][SUDOKU_MAX_SIDE];	//Ячейки блока
	uint8_t		bx;			//X координата начала сектора
	uint8_t		ex;			//X координата окончания сектора
	uint8_t		by;			//Y координата начала сектора
	uint8_t		ey;			//Y координата окончания сектора
} bsector_s;



//Структура судоку
typedef struct type_sudoku_s{
	puzzle_t	type;		//Тип пазла судоку
	u_char		**board;	//Итоговая доска пазла
	uint32_t	layers;		//Количество слоев доски
	uint32_t	side;		//Количество ячеек в блоке и количество блоков на доске
	uint32_t	line;		//Количество ячеек в строке / столбце (side * side)
	uint32_t	seed;		//Начальное значение для генератора случайных чисел
} sudoku_s;





/***********************************************************************
 * Функции
 **********************************************************************/

sudoku_s *		sudokuCreate(puzzle_t type, uint32_t seed);	//Создание структуры sudoku_s
void			sudokuFree(sudoku_s * sudoku);		//Освобождение памяти, занятой sudoku

void			sudokuPrintBoard(sudoku_s * sudoku);	//

void			sudokuFillLayer(sudoku_s * sudoku, uint32_t z);	//Заполнение полей




#ifdef __cplusplus
}
#endif

#endif //_XGEXTSUDOKU_H
