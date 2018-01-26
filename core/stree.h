/***********************************************************************
 * XG SERVER
 * core/stree.h
 * Текстовое дерево
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   



#ifndef _XGSTREE_H
#define _XGSTREE_H

#ifdef __cplusplus
extern "C" {
#endif


/***********************************************************************
 * Подключаемые заголовки
 **********************************************************************/
#include <limits.h>
#include "core.h"

//Максимальная глубина вложенности дерева (максимальная длинна ключа)
#define STREE_DEPTH	64


/***********************************************************************
 * Константы
 **********************************************************************/
 



/***********************************************************************
 * Объявления и декларации
 **********************************************************************/

//Флаг, указывающий что нода содержит реальные данные
#define SNODE_REAL	BIT(1)

//Флаг, указывающий что данные ноды при удалении должны быть удалены
#define SNODE_FREE	BIT(2)

//Флаг, указывающий, что данные ноды при существовании ноды, должны быть заменены новыми данными
//В противном случае, данные не меняются
#define SNODE_REPLACE	BIT(3)



/***********************************************************************
 * Структуры
 **********************************************************************/
typedef struct		type_stree_s	stree_s;	//Текстовое дерево
typedef struct		type_snode_s	snode_s;	//Элемент текстового дерева



//Элемент текстового дерева
typedef struct type_snode_s{
	snode_s		* parent;		//Родительская нода
	snode_s		* child;		//Дочерние ноды
	snode_s		* next;			//Следующая нода на этом уровне
	void		* data;			//Данные
	u_char		ch;				//Символ
	u_char		flags;			//Флаги ноды
} snode_s;



//Текстовое дерево
typedef struct type_stree_s{
	snode_s		* root[PRINT_INDEX_MAX + 1];
	free_cb		ffree;
} stree_s;



/***********************************************************************
 * Функции
 **********************************************************************/
void	streePrint(stree_s * stree);	//
inline stree_s *	streeNew(free_cb ffree);	//Создание дерева
inline snode_s *	snodeNew(u_char ch);	//Создание ноды дерева
inline void			snodeFree(snode_s * snode);	//Освобождение ноды дерева
inline snode_s *	snodeExtract(snode_s * snode);	//Извлекает ноду из дерева и возвращает указатель на нее
snode_s *			streeSet(stree_s * stree, const char * key, void * data, u_char flags);	//Добавляет в дерево новый элемент и возвращает указатель на него или NULL в случае ошибки
snode_s *			streeGet(stree_s * stree, const char * key);	//Ищет в дереве ноду с указанным ключем и возвращает на нее указатель или NULL если нода не найдена
void				snodeDelete(stree_s * stree, snode_s * snode);	//Удаляет ноду и данные ноды, если нода не используется
void				streeDelete(stree_s * stree, const char * key);	//Ищет в дереве ноду с указанным ключем и удаляет ее


#ifdef __cplusplus
}
#endif

#endif //_XGSTREE_H
