/***********************************************************************
 * XG SERVER
 * framework/template.h
 * Работа с HTML шаблонами
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   



#ifndef _XG_FRAMEWORK_TEMPLATE_H
#define _XG_FRAMEWORK_TEMPLATE_H

#ifdef __cplusplus
extern "C" {
#endif


/***********************************************************************
 * Подключаемые заголовки
 **********************************************************************/

#include "core.h"
#include "kv.h"



/***********************************************************************
 * Константы
 **********************************************************************/






/***********************************************************************
 * Объявления и декларации
 **********************************************************************/



/***********************************************************************
 * Структуры
 **********************************************************************/
typedef struct		type_template_s		template_s;


//Структура шаблона
typedef struct type_template_s{
	buffer_s 		* content;			//Буфер данных, содержащий контент
	kv_s			* binds;			//Макроподстановки типа ключ-> значение
	template_s		* next;				//Для IDLE списка
} template_s;






/***********************************************************************
 * Функции
 **********************************************************************/

template_s *	templateCreate(const char * filename, const char * path);	//Создает новый шаблонизатор
void			templateFree(void * ptr);	//Освобождает память, занятую шаблонизатором
bool			templateFromFile(template_s * template, const char * filename, const char * path);	//Загрузка шаблона из указанного файла
bool			templateFromString(template_s * template, const char * src, uint32_t ilen);	//Загрузка шаблона из строки
bool			templateFromStringS(template_s * template, const_string_s * src);	//Загрузка шаблона из строки string_s
bool			templateFromBuffer(template_s * template, buffer_s * buf);	//Загрузка шаблона из буфера buffer_s

kv_s *			templateGetBinds(template_s * template);	//Возвращает указатель на структуру kv_s списка макроподстановок
inline kv_s *	templateBindNull(template_s * template, const char * path);
inline kv_s *	templateBindBool(template_s * template, const char * path, bool value);
inline kv_s *	templateBindInt64(template_s * template, const char * path, int64_t value);
inline kv_s *	templateBindUInt64(template_s * template, const char * path, uint64_t value);
inline kv_s *	templateBindInt32(template_s * template, const char * path, int32_t value);
inline kv_s *	templateBindUInt32(template_s * template, const char * path, uint32_t value);
inline kv_s *	templateBindDouble(template_s * template, const char * path, double value);
inline kv_s *	templateBindString(template_s * template, const char * path, const char * str, uint32_t len);
inline kv_s *	templateBindStringPtr(template_s * template, const char * path, char * str, uint32_t len);
inline kv_s *	templateBindDatetime(template_s * template, const char * path, time_t time, const char * format);

buffer_s *		templateParse(template_s * template, const char * language);













#ifdef __cplusplus
}
#endif

#endif //_XG_FRAMEWORK_TEMPLATE_H
