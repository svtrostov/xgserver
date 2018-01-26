/***********************************************************************
 * XG SERVER
 * core/kv.c
 * Работа с KV (Key -> Value)
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/

#ifndef _XGKV_H
#define _XGKV_H

#ifdef __cplusplus
extern "C" {
#endif


/***********************************************************************
 * Подключаемые заголовки
 **********************************************************************/
#include "core.h"

//Количество элементов KV, выделяемых для IDLE списка
static const uint32_t kv_idle_list_size = FD_SETSIZE * 32;

#define KV_KEY_NAME_IS_DYNAMIC0
#ifndef KV_KEY_NAME_IS_DYNAMIC
#define KV_KEY_NAME_LEN 64
#endif



/***********************************************************************
 * Объявления и декларации
 **********************************************************************/

/*Флаги при парсинге JSON в дерево KV*/
typedef enum{
	KVJF_ALLOW_NONE		= 0,
	KVJF_ALLOW_INCLUDE	= 1 << 0,
	KVJF_ALLOW_STOP		= 1 << 1,
	KVJF_ALLOW_ALL		= KVJF_ALLOW_INCLUDE | KVJF_ALLOW_STOP
} kv_jsonp_flag;


/*Типы данных KV ключ->значение*/
typedef enum{
	KV_NULL		= 0,	//NULL Null
	KV_BOOL		= 1,	//Булево значение True / False
	KV_INT		= 2,	//Целое число 123
	KV_DOUBLE	= 3,	//Вещественное число 123.456
	KV_STRING	= 4,	//Текстовое значение ""
	KV_JSON		= 5,	//Текстовое значение в формате JSON ""
	KV_ARRAY	= 6,	//Массив порядковый []
	KV_OBJECT	= 7,	//Массив ассоциативный {}
	KV_DATETIME	= 8,	//Дата и время
	//Внутренние типы данных
	KV_POINTER	= 9,	//Внутренний тип, говорит что значение представлено в виде указателя на какую-то область памяти (void *)
	KV_FUNCTION	= 10	//Внутренний тип, говорит что значение представлено в виде указателя на функцию (void *)
} kv_t;


/*Тип формата вывода KV*/
typedef enum{
	KVF_JSON		= 1,	//Вывод в формате JSON
	KVF_URLQUERY	= 2,	//Вывод в формате URL query
	KVF_HEADERS		= 3		//Вывод в формате HTTP заголовков работает только с типом KV_OBJECT без вложенности
} kv_format_t;


/*Тип поведения при нахождении KV с дублирующим ключем*/
typedef enum{
	KV_BREAK		= 0,	//Не вставлять новую запись
	KV_REPLACE		= 1,	//Заменить существующую запись новой
	KV_INSERT		= 2		//Вставить новую запить
} kv_rewrite_rule;


/*Результаты работы функций*/
typedef enum{
	KVR_OK			= 0,	//Без ошибок
	KVR_ERROR		= 1,	//Ошибка
	KVR_EXISTS		= 2		//Запись существует
} kv_result;


/*
Прототип пользовательской функции, отвечающий
за уничтожение данных типа KV_POINTER
void * - указатель на данные
*/
typedef void (*v_pointer_free_cb)(void *);



/***********************************************************************
 * Структуры
 **********************************************************************/
typedef struct type_kv_s kv_s;


//Значение
typedef union{
	bool				v_bool;		//Значение для типа KV_BOOL
	int64_t				v_int;		//Значение для типа KV_INT
	double				v_double;	//Значение для типа KV_DOUBLE
	string_s			v_string;	//Значение для типа KV_STRING
	string_s			v_json;		//Значение для типа KV_JSON
	struct{
		kv_s * 	first;	//Первый дочерний элемент (элементы) <-- на уровень ниже (для типа KV_ARRAY и KV_OBJECT)
		kv_s * 	last;	//Последний дочерний элемент (элементы) <-- на уровень ниже (для типа KV_ARRAY и KV_OBJECT)
	} v_list;
	struct{
		time_t			ts;			//Время в секундах от начала эпохи
		const char *	format;		//Формат вывода времени в сроку
	} v_datetime;
	//Специальные типы KV, только для внутреннего использования сервером
	//Не имеют никакого отношения к JSON,  не сохраняется, не выводится через echo
	void *				v_function;	//Указатель на функцию,  тип KV_FUNCTION
	struct{
		void * 				ptr;	//Указатель на область памяти,  тип KV_POINTER
		v_pointer_free_cb	free;	//Вызываемая функция для освобождения области памяти, на которую ссылается ptr
	} v_pointer;
} kv_value;


/*Структура записи ключ->значение*/
typedef struct type_kv_s{

	//Навигация
	kv_s * parent;	//Родительский элемент <-- на уровень выше
	kv_s * next;	//Следующий элемент <-- на текущем уровне
	kv_s * prev;	//Предыдущий элемент <-- на текущем уровне

	//Идентификация
	kv_t type;					//Тип текущего элемента

#ifdef KV_KEY_NAME_IS_DYNAMIC
	char * key_name;			//Имя текущего элемента (ключ)
#else
	char	key_name[KV_KEY_NAME_LEN + 1];		//Имя текущего элемента (ключ)
#endif
	uint32_t key_len;			//Длинна ключа (length)
	uint32_t key_hash;			//Хэш ключа

	kv_value value;				//Значение

	int		flags;				//дополнительные флаги применяемые к данному объекту KV (задаются произвольно)

} kv_s;




/***********************************************************************
 * Функции: core/kv.c - Работа с KV (key -> value)
 **********************************************************************/


inline kv_s *	kvNew(void);	//Создание KV
inline kv_s *	kvNewRoot(void);	//Создание KV - рут элемент
kv_s *			kvClear(kv_s * node);	//Очистка значения KV
kv_s *			kvRemove(kv_s * node);	//Изъятие KV из структуры KV
void			kvFree(kv_s * node);	//Освобождение памяти, занятой KV
kv_s *			kvSetKey(kv_s * node, const char * key_name, uint32_t key_len);	//Изменяет имя ключа
kv_s *			kvSetType(kv_s * node, kv_t new_type);	//Изменяет тип данных в элементе
bool			kvIsEmpty(kv_s * node);	//Проверяет, пустое ли значение KV или нет (пустая строка, пустой массив, пустой объект, 0 или false)
kv_s * 			kvMerge(kv_s * to, kv_s * from, kv_rewrite_rule rewrite);	//Объединяет KV объекты типа KV_OBJECT, перенося все содержимое в объект to из from, объект from уничтожается
kv_s *			kvReplace(kv_s * dst, kv_s * src);	//Заменяет запись dst записью src, при этом остается только одна запись dst (src удаляется)
kv_s *			kvCopy(kv_s * dst, kv_s * src);	//Копирует объекты KV из src в dst, возвращает dst
kv_s *			kvFill(kv_s * dst, kv_s * src);	//Заполняет объект dst значениями из src, при совпадении ключей в dst и src
kv_s *			kvIntersect(kv_s * kv1, kv_s * kv2);	//Создает новый объект KV, который объединяет kv1 и kv2 по совпадающим ключам, при этом в результирующем объекте используются значения из kv1
kv_s *			kvSearch(kv_s * parent, const char * key_name, uint32_t key_len);	//Ищет KV с указанным именем в родительской ноде
kv_s *			kvSearchHash(kv_s * parent, const char * key_name, uint32_t key_len, uint32_t hash);	//Ищет KV с указанным именем в родительской ноде
kv_result		kvInsert(kv_s * parent, kv_s * child, kv_rewrite_rule rewrite);	//Добавляет дочерний KV родителю KV
kv_s *			kvAppend(kv_s * parent, const char * key_name, uint32_t key_len, kv_rewrite_rule rewrite);	//Создает дочерний KV в родительском элементе
inline kv_s *	kvSetString(kv_s * node, const char * str, uint32_t len);	//Добавляет в KV значение типа KV_STRING, str копируется во внутреннюю переменную
inline kv_s *	kvSetStringPtr(kv_s * node, char * str, uint32_t len);	//Добавляет в KV значение типа KV_STRING, внутренней переменной присваивается указатель на str без копирования
inline kv_s *	kvSetJson(kv_s * node, const char * str, uint32_t len);	//Добавляет в KV значение типа KV_JSON, str копируется во внутреннюю переменную
inline kv_s *	kvSetJsonPtr(kv_s * node, char * str, uint32_t len);	//Добавляет в KV значение типа KV_JSON, внутренней переменной присваивается указатель на str без копирования
inline kv_s *	kvSetBool(kv_s * node, bool v_bool);	//Добавляет в KV значение типа KV_BOOL
inline kv_s *	kvSetInt(kv_s * node, int64_t v_int);	//Добавляет в KV значение типа KV_INT
inline kv_s *	kvSetDouble(kv_s * node, double v_double);	//Добавляет в KV значение типа KV_DOUBLE
inline kv_s *	kvSetNull(kv_s * node);	//Добавляет в KV значение типа KV_NULL
inline kv_s *	kvSetFunction(kv_s * node, void * v_function);	//Добавляет в KV значение типа KV_FUNCTION
inline kv_s *	kvSetPointer(kv_s * node, void * ptr, v_pointer_free_cb cb);	//Добавляет в KV значение типа KV_POINTER
inline kv_s *	kvSetDatetime(kv_s * node, time_t ts, const char * format);	//Добавляет в KV значение типа KV_DATETIME
inline kv_s *	kvAppendDatetime(kv_s * parent, const char * key_name, time_t ts, const char * format, kv_rewrite_rule rewrite);	//Добавляет дочерний KV типа KV_DATETIME родителю 
inline kv_s *	kvAppendNull(kv_s * parent, const char * key_name, kv_rewrite_rule rewrite);	//Добавляет дочерний KV типа KV_NULL родителю 
inline kv_s *	kvAppendBool(kv_s * parent, const char * key_name, bool value, kv_rewrite_rule rewrite);	//Добавляет дочерний KV типа KV_BOOL родителю 
inline kv_s *	kvAppendInt(kv_s * parent, const char * key_name, int64_t value, kv_rewrite_rule rewrite);	//Добавляет дочерний KV типа KV_INT родителю 
inline kv_s *	kvAppendDouble(kv_s * parent, const char * key_name, double value, kv_rewrite_rule rewrite);	//Добавляет дочерний KV типа KV_DOUBLE родителю 
inline kv_s *	kvAppendString(kv_s * parent, const char * key_name, const char * value, uint32_t value_len, kv_rewrite_rule rewrite);	//Добавляет дочерний KV типа KV_STRING родителю 
inline kv_s *	kvAppendStringPtr(kv_s * parent, const char * key_name, char * value, uint32_t value_len, kv_rewrite_rule rewrite);	//Добавляет дочерний KV типа KV_STRING родителю 
inline kv_s *	kvAppendJson(kv_s * parent, const char * key_name, const char * value, uint32_t value_len, kv_rewrite_rule rewrite);	//Добавляет дочерний KV типа KV_JSON родителю 
inline kv_s *	kvAppendArray(kv_s * parent, const char * key_name, kv_rewrite_rule rewrite);	//Добавляет дочерний KV типа KV_ARRAY родителю 
inline kv_s *	kvAppendObject(kv_s * parent, const char * key_name, kv_rewrite_rule rewrite);	//Добавляет дочерний KV типа KV_OBJECT родителю 
inline kv_s *	kvAppendFunction(kv_s * parent, const char * key_name, void * value, kv_rewrite_rule rewrite);	//Добавляет дочерний KV типа KV_FUNCTION родителю 
inline kv_s *	kvAppendPointer(kv_s * parent, const char * key_name, void * ptr, v_pointer_free_cb cb, kv_rewrite_rule rewrite);	//Добавляет дочерний KV типа KV_POINTER родителю 

kv_s *			kvFromJsonString(const char * json, kv_jsonp_flag flags);	//Создание дерева KV из JSON текста
kv_s *			kvFromJsonFile(const char * filename, kv_jsonp_flag flags);	//Создание дерева KV из JSON файла
kv_s *			kvFromQueryString(const char * query);	//Создание дерева KV из query строки запроса GET или POST (application/x-www-form-urlencoded)

buffer_s *		kvAsString(kv_s * kv, buffer_s * buf);	//Преобразует значение KV в строку
buffer_s *		kvEcho(kv_s * root, kv_format_t format, buffer_s * buf);	//Вывод дерева KV в строку
void			kvEchoJson(buffer_s * buf, kv_s * current);	//Вывод значения KV в буффер buffer_s в формате JSON
void			kvEchoQuery(buffer_s * buf, kv_s * current, uint32_t depth);	//Вывод значения KV в буффер buffer_s в формате URL Query
void			kvEchoHeaders(buffer_s * buf, kv_s * parent);	//Вывод значения KV в буффер buffer_s в формате HTTP Headers
const char *	kvEchoType(kv_t type);	//Возвращает тип KV в виде текста

kv_s *			kvGetChild(kv_s * parent, const char * path);	//Возвращает дочерний KV исходя из указанного имени ключа или NULL, обновленная версия kvSearch
bool 			kvGetAsBool(kv_s * parent, const char * path, bool def);	//
int64_t			kvGetAsInt64(kv_s * parent, const char * path, int64_t def);	//
uint64_t		kvGetAsUInt64(kv_s * parent, const char * path, uint64_t def);	//
int32_t			kvGetAsInt32(kv_s * parent, const char * path, int32_t def);	//
uint32_t		kvGetAsUInt32(kv_s * parent, const char * path, uint32_t def);	//
double			kvGetAsDouble(kv_s * parent, const char * path, double def);	//
const char *	kvGetAsString(kv_s * parent, const char * path, const char * def);	//
const_string_s *	kvGetAsStringS(kv_s * parent, const char * path, const_string_s * def);	//
const char *	kvGetAsJson(kv_s * parent, const char * path, const char * def);	//
const_string_s *	kvGetAsJsonS(kv_s * parent, const char * path, const_string_s * def);	//
void *			kvGetAsFunction(kv_s * parent, const char * path, void * def);	//
void *			kvGetAsPointer(kv_s * parent, const char * path, void * def);	//

kv_s *			kvGetByIndex(kv_s * parent, uint32_t index);	//Возвращает KV по индексу элемента
kv_s *			kvGetByPath(kv_s * root, const char * path);	//Возвращает KV исходя из указанного пути или NULL

bool 			kvGetBoolByPath(kv_s * root, const char * path, bool def);	//Получение значения типа bool
int64_t			kvGetIntByPath(kv_s * root, const char * path, int64_t def);	//Получение значения типа int64_t
double			kvGetDoubleByPath(kv_s * root, const char * path, double def);	//Получение значения типа double
const char *	kvGetStringByPath(kv_s * root, const char * path, const char * def);	//Получение значения типа char *
const_string_s * kvGetStringSByPath(kv_s * root, const char * path, const_string_s * def);	//Получение значения типа string_s *
const char *	kvGetJsonByPath(kv_s * root, const char * path, const char * def);	//Получение значения типа char *
const_string_s * kvGetJsonSByPath(kv_s * root, const char * path, const_string_s * def);	//Получение значения типа string_s *
void *			kvGetFunctionByPath(kv_s * root, const char * path, void * def);	//Получение значения типа void *
void *			kvGetPointerByPath(kv_s * root, const char * path, void * def);	//Получение значения типа void *

kv_s * 			kvGetRequire(kv_s * root, const char * path);	//Получение KV или фатальная ошибка если KV не найден
kv_s * 			kvGetRequireType(kv_s * root, const char * path, kv_t type);	//Получение KV опредленного типа или фатальная ошибка если KV не найден или тип данных не совпадает
bool 			kvGetRequireBool(kv_s * root, const char * path);	//
int64_t			kvGetRequireInt(kv_s * root, const char * path);	//
double			kvGetRequireDouble(kv_s * root, const char * path);	//
const char *	kvGetRequireString(kv_s * root, const char * path);	//
const_string_s * kvGetRequireStringS(kv_s * root, const char * path);	//
const char *	kvGetRequireJson(kv_s * root, const char * path);	//
const_string_s * kvGetRequireJsonS(kv_s * root, const char * path);	//
void *			kvGetRequireFunction(kv_s * root, const char * path);	//
void *			kvGetRequirePointer(kv_s * root, const char * path);	//

kv_s *			kvSetByPath(kv_s * root, const char * path);	//Создает KV по указанному пути и возвращает указатель на него

inline kv_s *	kvSetNullByPath(kv_s * root, const char * path);	//
inline kv_s *	kvSetBoolByPath(kv_s * root, const char * path, bool value);	//
inline kv_s *	kvSetIntByPath(kv_s * root, const char * path, int64_t value);	//
inline kv_s *	kvSetDoubleByPath(kv_s * root, const char * path, double value);	//
inline kv_s *	kvSetStringByPath(kv_s * root, const char * path, const char * str, uint32_t len);	//
inline kv_s *	kvSetStringPtrByPath(kv_s * root, const char * path, char * str, uint32_t len);	//
inline kv_s *	kvSetJsonByPath(kv_s * root, const char * path, const char * str, uint32_t len);	//
inline kv_s *	kvSetJsonPtrByPath(kv_s * root, const char * path, char * str, uint32_t len);	//
inline kv_s *	kvSetFunctionByPath(kv_s * root, const char * path, void * value);	//
inline kv_s *	kvSetPointerByPath(kv_s * root, const char * path, void * ptr, v_pointer_free_cb cb);	//
inline kv_s *	kvSetDatetimeByPath(kv_s * root, const char * path, time_t time, const char * format);	//


//Поиск в массиве или объекте KV по значениям
kv_s *			kvInArrayNull(kv_s * parent);	//
kv_s *			kvInArrayBool(kv_s * parent, bool term);	//
kv_s *			kvInArrayInt(kv_s * parent, int64_t term);	//
kv_s *			kvInArrayDouble(kv_s * parent, double term);	//
kv_s *			kvInArrayString(kv_s * parent, const char * term);	//
kv_s *			kvInArrayPointer(kv_s * parent, void * term);	//


#ifdef __cplusplus
}
#endif

#endif //_XGKV_H
