/***********************************************************************
 * XG SERVER
 * framework/language.h
 * Работа с языковыми локализациями
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   



#ifndef _XG_FRAMEWORK_LANGUAGE_H
#define _XG_FRAMEWORK_LANGUAGE_H

#ifdef __cplusplus
extern "C" {
#endif


/***********************************************************************
 * Подключаемые заголовки
 **********************************************************************/

#include "core.h"
#include "kv.h"

//Языковая локализация по-умолчанию
#define LANG_DEFAULT "en"

//Возвращает текстовую строку языковой локализации по указанному пути
#define lang(path, language) langGetValue(path, language, path)

//Возвращает текстовую строку языковой локализации по указанному пути а также длинну строки в n
#define langn(path, language, n) langGetValueN(path, language, path, n)

/***********************************************************************
 * Константы
 **********************************************************************/






/***********************************************************************
 * Объявления и декларации
 **********************************************************************/



/***********************************************************************
 * Структуры
 **********************************************************************/





/***********************************************************************
 * Функции
 **********************************************************************/
bool			langExists(const char * language, uint32_t language_n);	//Проверяет существование выбранной языковой локализации
const char *	langDefault(void);	//Возвращает языковую локализацию, установленную по-умолчанию
const char *	langSelect(const char * language, uint32_t language_n);	//Возвращает языковую локализацию, которую можно исползовать
const char *	langGetValue(const char * path, const char * language, const char * def);	//Возвращает значение 
const char *	langGetValueN(const char * path, const char * language, const char * def, uint32_t * olen);	//Возвращает значение 


#ifdef __cplusplus
}
#endif

#endif //_XG_FRAMEWORK_LANGUAGE_H
