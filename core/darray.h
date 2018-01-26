/***********************************************************************
 * XG SERVER
 * core/darray.h
 * Динамический массив
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   



#ifndef _XGDARRAY_H
#define _XGDARRAY_H

#ifdef __cplusplus
extern "C" {
#endif


/***********************************************************************
 * Подключаемые заголовки
 **********************************************************************/
#include <limits.h>
#include "core.h"

//Максимальная глубина вложенности дерева (максимальная длинна ключа)
#define DARRAY_LINE		UCHAR_MAX + 1


/***********************************************************************
 * Константы
 **********************************************************************/
 



/***********************************************************************
 * Объявления и декларации
 **********************************************************************/

//Типы данных динамического массива
typedef enum type_darray_type_e{
	DATYPE_POINTER 		= 0,	//Указатель на область памяти
	DATYPE_STRING 		= 1,	//Указатель на строку
	DATYPE_DOUBLE 		= 2,	//Число типа Double
	DATYPE_INTEGER		= 3		//Целое число типа int64
} darray_type_e;



/***********************************************************************
 * Структуры
 **********************************************************************/
typedef struct		type_darray_s	darray_s;	//Динамический массив


//Типы данных динамического масисва
typedef union{
	char	* v_char;
	void	* v_ptr;
	double	v_double;
	int64_t	v_int;
} darray_data_u;


//Динамический массив
typedef struct type_darray_s{
	darray_type_e	type;		//Тип данных в массиве
	darray_data_u	* array;	//Указатель на начало массива
	free_cb			ffree;		//Функция освобождения занятой памяти (применятеся только для DATYPE_POINTER и DATYPE_STRING)
	uint32_t		count;		//Количество занятых элементов массива
	uint32_t		allocated;	//Количество элементов массива, под которые была выделена память
	uint32_t		increment;	//Количество динамически добавляемых элементов в массив
	uint32_t		offset;		//Сдвиг по ID в сторону уменьшения (offset = 0 , id = 10 -> 10; offset = 3, id = 10 -> 10-3 = 7)
} darray_s;





/***********************************************************************
 * Функции
 **********************************************************************/

darray_s *		darrayCreate(darray_type_e type, uint32_t allocated, uint32_t increment);	//Создание массива
void			darrayFree(darray_s * da);	//Освобождение памяти, занятой под массив
darray_s *		darraySetIncrement(darray_s * da, uint32_t increment);	//Изменение значения инкремента
darray_s *		darraySetOffset(darray_s * da, uint32_t offset);	//Изменение значения инкремента
darray_s *		darraySetFree(darray_s * da, free_cb ffree);	//Задает функцию освобождения памяти для элементов массива
inline int32_t	darrayId2Index(darray_s * da, uint32_t id);	//Преобразует ID в индекс массива
inline uint32_t	darrayIndex2Id(darray_s * da, uint32_t index);	//Преобразует индекс массива в ID
darray_data_u *	darraySet(darray_s * da, uint32_t id, void * data);	//Устанавливает значение для указанного ID в пределах от 0 до da->count
darray_data_u *	darraySetId(darray_s * da, uint32_t id, void * data);	//Увеличивает размер массива до указанного ID и задает его значение
darray_data_u *	darrayPush(darray_s * da, void * data);	//Добавляет новый элемент в конец массива
inline darray_data_u * darrayGet(darray_s * da, uint32_t id);	//Возарвщвет указатель на элемент массива по ID
inline int64_t	darrayGetInt(darray_s * da, uint32_t id, int64_t def);	//
inline double	darrayGetDouble(darray_s * da, uint32_t id, double def);	//
inline char *	darrayGetChar(darray_s * da, uint32_t id, char * def);	//
inline void *	darrayGetPointer(darray_s * da, uint32_t id, void * def);	//













#ifdef __cplusplus
}
#endif

#endif //_XGDARRAY_H
