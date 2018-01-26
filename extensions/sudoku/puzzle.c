/***********************************************************************
 * XGSERVER EXTENSION
 * puzzle.c
 * Расширение: генератор SUDOKU пазлов
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/

#include <math.h>
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


//SuDoKu карта символов
static const u_char _puzzleMap[_SUDOKUMAPSIZE] = _SUDOKUMAP;

//Массив получения индекса символа в карте
static const int _mapidx[256] = {
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	9,	0,	1,	2,	3,	4,	5,	6,	7,	8,	-1,	-1,	-1,	-1,	-1,	-1,	/* 0123456789...... */
	-1,	10,	11,	12,	13,	14,	15,	16,	17,	18,	19,	20,	21,	22,	23,	24,	/* .ABCDEFGHIJKLMNO */
	25,	26,	27,	28,	29,	30,	31,	32,	33,	34,	35,	-1,	-1,	-1,	-1,	-1,	/* PQRSTUVWXYZ..... */
	-1,	10,	11,	12,	13,	14,	15,	16,	17,	18,	19,	20,	21,	22,	23,	24,	/* .abcdefghijklmno */
	25,	26,	27,	28,	29,	30,	31,	32,	33,	34,	35,	-1,	-1,	-1,	-1,	-1,	/* pqrstuvwxyz..... */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1	/* ................ */
};



#define _cell(x,y,z) sudoku->board[x + y * sudoku->line + z * sudoku->line * sudoku->line]



/***********************************************************************
 * Функции
 **********************************************************************/

/*
 * Вычисление индекса блока в зависимости от заданных координат
 */
static int
_blockIndex(sudoku_s * sudoku, int x, int y){
	int result = (int)floor(x / sudoku->side);
	return (int)(floor(y / sudoku->side) * sudoku->side) + result;
}


/*
 * Создание структуры sudoku_s
 */
sudoku_s *
sudokuCreate(puzzle_t type, uint32_t seed){
	sudoku_s * sudoku = (sudoku_s *)mNewZ(sizeof(sudoku_s));
	sudoku->type = type;
	sudoku->seed = seed;
	switch(type){
		//Классический пазл 9х9 (123456789 в 1 слой)
		case SPUZZLE_9X9:
			sudoku->side	= 3;
			sudoku->line	= 9;
			sudoku->layers	= 1;
		break;

		//3D пазл 9х9х9  (123456789 в 9 слоев)
		case SPUZZLE_9X93D:
			sudoku->side	= 3;
			sudoku->line	= 9;
			sudoku->layers	= 9;
		break;

		//Пазл 16х16  (0123456789ABCDEF в 1 слой)
		case SPUZZLE_16X16:
			sudoku->side	= 4;
			sudoku->line	= 16;
			sudoku->layers	= 1;
		break;

		//Пазл 25х25  (0123456789ABCDEFGHIJKLMNO в 1 слой)
		case SPUZZLE_25X25:
			sudoku->side	= 5;
			sudoku->line	= 25;
			sudoku->layers	= 1;
		break;

		//Расширенный пазл 36х36  (0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ в 1 слой)
		case SPUZZLE_36X36:
			sudoku->side	= 6;
			sudoku->line	= 36;
			sudoku->layers	= 1;
		break;
	}//switch

	sudoku->board	= mNewZ(sudoku->line * sudoku->line * sudoku->layers);


	sudokuFillLayer(sudoku, 0);

	sudokuPrintBoard(sudoku);

	return sudoku;
}//END: sudokuCreate


/*
 * Освобождение памяти, занятой sudoku
 */
void
sudokuFree(sudoku_s * sudoku){
	mFree(sudoku->board);
	mFree(sudoku);
}//END: sudokuFree


/*
 * Вывод на экран пазла
 */
void
sudokuPrintBoard(sudoku_s * sudoku){
	if(!sudoku || !sudoku->board) return;
	uint32_t x, y, z;
	u_char ch;

	for(z = 0; z < sudoku->layers; z++){
		printf("\nLayer %u:\n---------------------------------\n", (z+1));
		for(y = 0; y < sudoku->line; y++){
			printf("Line %.2u:\t", (y+1));
			for(x = 0; x < sudoku->line; x++){
				ch = _cell(x, y, z);
				if(!ch) ch = '-';
				printf("%c ", ch);
				if(x % (sudoku->side) == (sudoku->side-1)) printf("\t");
			}//x
			if(y % (sudoku->side) == (sudoku->side-1)) printf("\n");
			printf("\n");
		}//y
	}//z

}//END: sudokuPrintBoard



/***********************************************************************
 * Генерация пазла указанного типа
 **********************************************************************/


//Проверяет, находится ли заданное значение по-горизонтали (в строке)
static bool
_valueInY(sudoku_s * sudoku, u_char value, uint32_t y, uint32_t z){
	uint32_t x;
	for(x=0;x<sudoku->line;x++){
		if(value == _cell(x,y,z)) return true;
	}
	return false;
}


//Проверяет, находится ли заданное значение по-вертикали (в столбце)
static bool
_valueInX(sudoku_s * sudoku, u_char value, uint32_t x, uint32_t z){
	uint32_t y;
		for(y=0;y<sudoku->line;y++){
			if(value == _cell(x,y,z)) return true;
		}
	return false;
}



/*
 * Заполнение полей
 */
void
sudokuFillLayer(sudoku_s * sudoku, uint32_t z){
	if(!sudoku) return;

	int index, b, x, y, i;
	u_char ch;
	bmask_s  * mask = mNewZ(sizeof(bmask_s *) * sudoku->line);
	bblock_s * blocks = mNewZ(sizeof(bblock_s) * sudoku->line);
	bblock_s * block;
	bcell_s  * bcell;

	//Копия карты символов и их рандомное перемешивание
	u_char * map = mNew(sudoku->line);
	stringCopyN(map, _puzzleMap, sudoku->line);
	for(i=0; i<sudoku->line; i++){
		x = randomValue(&sudoku->seed) % sudoku->line;
		ch = map[i];
		map[i] = map[x];
		map[x] = ch;
	}

	//Подготовка секторов
	for(b = 0; b < sudoku->line; b++){

		//Создание позиционной маски символов в секторе
		mask[b] = mNewZ(sizeof(bmask_s));
		mask[b]->x = (b % sudoku->side);
		mask[b]->y = floor(b / sudoku->side);

		blocks[b].bx = (b % sudoku->side) * sudoku->side;
		blocks[b].by = floor(b / sudoku->side) * sudoku->side;
		blocks[b].ex = blocks[b].bx + sudoku->side - 1;
		blocks[b].ey = blocks[b].by + sudoku->side - 1;
		//printf("%.2u:\tbx=%u\tby=%u\tex=%u\tey=%u\n",b,blocks[b].bx,blocks[b].by,blocks[b].ex,blocks[b].ey);
		blocks[b].cells = mNewZ(sizeof(bcell_s *) * sudoku->line);
		index = 0;
		for(y = blocks[b].by; y <= blocks[b].ey; y++){
			for(x = blocks[b].bx; x <= blocks[b].ex; x++){
				if(_cell(x,y,z) == 0){
					bcell = mNewZ(sizeof(bcell_s));
					bcell->x = x;
					bcell->y = y;
					blocks[b].cells[index] = bcell;
					index++;
				}
			}//x
		}//y
		blocks[b].available = index;
		//Перемешиваем позиции случайным образом
		for(index = 0; index < sudoku->line; index++){
			bcell = blocks[b].cells[index];
			x = randomValue(&sudoku->seed) % sudoku->line;
			blocks[b].cells[index] = blocks[b].cells[x];
			blocks[b].cells[x] = bcell;
		}
	}//block

	//Построение судоку
	for(index = 0; index < sudoku->line; index++){
		ch = map[index];
		for(b = 0; b < sudoku->line; b++){
			block = &blocks[b];

			for(i=0;i<block->available;i++){
				bcell = block->cells[i];

				if(_valueInY(sudoku, ch, bcell->y, z) || _valueInX(sudoku, ch, bcell->x, z) || _cell(bcell->x, bcell->y, z) != 0) continue;

				_cell(bcell->x, bcell->y, z) = ch;

				block->available--;
				block->cells[i] = block->cells[block->available];
				mFree(bcell);
				break;
			}//i
		}//block
		sudokuPrintBoard(sudoku);
	}//index


	for(b = 0; b < sudoku->line; b++){
		mFree(blocks[b].cells);
	}
	mFree(blocks);
	mFree(map);

}//END: sudikuFillBoard
