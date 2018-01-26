/***********************************************************************
 * XG SERVER
 * core/session.c
 * Работа с сессиями
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   



#ifndef _XGSESSION_H
#define _XGSESSION_H

#ifdef __cplusplus
extern "C" {
#endif


/***********************************************************************
 * Подключаемые заголовки
 **********************************************************************/

#include "core.h"
#include "kv.h"



//Количество элементов KV, выделяемых для IDLE списка
static const uint32_t session_idle_list_size = 4096;



/***********************************************************************
 * Объявления и декларации
 **********************************************************************/

//Статус сессии
typedef enum{
	SESSION_CREATED		= BIT(1),	//Сессия создана
	SESSION_LOADED		= BIT(2),	//Сессия загружена
	SESSION_CHANGED		= BIT(3),	//Сессия изменена
	SESSION_SAVED		= BIT(4),	//Сессия сохранена
	SESSION_CACHED		= BIT(5)	//Сессия закеширована
} session_state_e;





/***********************************************************************
 * Структуры
 **********************************************************************/

//Длинна строки, занимаемая session_id
#define SESSION_ID_LEN 32

typedef struct	type_scitem_s		scitem_s;
typedef struct	type_sclist_s		sclist_s;
typedef struct	type_sentry_s		sentry_s;
typedef struct	type_sccell_s		sccell_s;
typedef struct	type_session_s		session_s;


//Структура настроек сессий
typedef struct{
	time_t		timeout;			//Таймаут между использованием сессии до истечения ее срока действия, секунд (0 - отключен)
	time_t		lifetime;			//Таймаут жизни сессии вне зависимости от частоты использования, секунд (0 - отключен)
	string_s 	path;				//Путь к папке с сессиями
	char *		session_name;		//Название переменной в GET POST или COOKIE, отвечающая за хранение ID сессии
	uint32_t	cache_limit;		//Максимальное количество сессий, которые могут быть в кэше
} session_options_s;


//Структура сессии
typedef struct type_session_s{

	char				session_id[SESSION_ID_LEN + 1];		//ID сессии (имя файла) 32 + 1 байт для \0
	socket_addr_s		ip_addr;			//Буфер хранения IP адреса клиента 
	uint32_t			user_agent;			//Хеш UserAgent'a

	uint32_t			user_id;			//Идентификатор аутентифицированного пользователя

	time_t				create_ts;			//Время создания сессии (секунд от начала эпохи)
	time_t				open_ts;			//Время последнего открытия сессии клиентом
	time_t				timeout;			//Таймаут между использованием сессии до истечения ее срока действия, секунд (0 - отключен)
	time_t				lifetime;			//Таймаут жизни сессии вне зависимости от частоты использования, секунд (0 - отключен)

	kv_s				* kv;				//Переменные сессии KV

	//Внутренние переменные, обнуляемые при загрузке сессии из файла
	session_state_e		state;				//Состояние сессии
	scitem_s			* cache;			//Указатель на элемент списка кеша сессий
	session_s			* next;				//Используется только для idle списка сессий

} session_s;



/*
 * Session cache
 */

//Структура элемента списка кеша сессий
typedef struct	type_scitem_s{
	session_s	* session;	//Указатель на сессию
	sccell_s	* cell;		//Указатель на элемент хеш таблицы кеша сессий
	scitem_s	* next;		//Следующий элемент
	scitem_s	* prev;		//Предыдущий элемент
	size_t		using;		//Количество экземпляров сессии, используемых в настоящий момент
} scitem_s;	//session cache item



//Структура списка кеша сессий
typedef struct	type_sclist_s{
	scitem_s	* first;	//Первый элемент
	scitem_s	* last;		//Последний элемент
	size_t		count;		//Текущее количество элементов в списке
} sclist_s;	//session cache list



//Структура элемента хеш таблицы кеша сессий
typedef struct	type_sccell_s{
	scitem_s	* item;
	sentry_s	* entry;
	sccell_s	* next;
	sccell_s	* prev;
} sccell_s;


//Структура вхождения хеш таблицы
typedef struct	type_sentry_s{
	sccell_s	* first;	//Первый элемент
} sentry_s;





//Структура кэша сессии
typedef struct	type_scache_s{
	sclist_s	list;		//Список сессий
	size_t		limit;		//Лимит сессий
	sentry_s	** entries;	//Указатель на массив вхождений хеш таблицы
	size_t		sizeofentriesline;	//Длинна строки entries
} scache_s;







/***********************************************************************
 * Константы
 **********************************************************************/

//Размер инкремента для буфера данных сессии
static const uint32_t session_buffer_increment = 2048; //по умолчанию 2 килобайт

//Ноль
static const uint32_t session_zero = 0;

//Блок [граница] - значение
static const uint32_t session_boundary = 0xDEDDED90;

//Блок [начало вложения KV] - значение
static const uint32_t session_kvbegin = 0xDEAD1111;

//Блок [окончание вложения KV] - значение
static const uint32_t session_kvend = 0xDEAD0000;

//Размер типа session_s (байт)
static const uint32_t session_s_size = sizeof(session_s);


/*
 * Константы позиционирования в файле сессии
 * 
 * Структура файла сессии выглядит следубщим образом
 * [граница][размер файла][размер структуры session_s][структура session_s][граница][начало вложения KV][Данные KV][окончание вложения KV][граница]
 * блоки [граница], [размер файла], [размер структуры session_s], [начало вложения KV], [окончание вложения KV] имеют тип uint32_t и размер sizeof(uint32_t)
 * [структура session_s] - тип session_s и размер sizeof(session_s)
 * [Данные KV] - произвольный размер
 * 
 * Структура записи KV
 * [длинна ключа][ключ][хэш ключа][тип данных][блок данных]
 * Если ключ имеет нулевую длинну, блоки [ключ] и [хэш ключа] отсутствуют
 * [длинна ключа = 0][тип данных][блок данных]
 * 
 * Структура [блок данных] в зависимости от типа данных
 * KV_NULL 		- отсуствует
 * KV_BOOL		- [bool значение размером sizeof(bool)]
 * KV_INT		- [int64_t значение размером sizeof(int64_t)]
 * KV_DOUBLE	- [double значение размером sizeof(double)]
 * KV_STRING	- [длинна строки (uint32_t)][текст]
 * KV_DATETIME	- [time_t значение размером sizeof(time_t)][длинна строки формата даты/времени (uint32_t)][строка формата даты/времени]
 * KV_ARRAY		- [начало вложения KV]...[окончание вложения KV]
 * KV_OBJECT	- [начало вложения KV]...[окончание вложения KV]
 */

/*
 * Позиционирование блоков в файле (от начала файла)
 */

//Первый блок границы
static const uint32_t session_boundary_first_pos = 0;

//Блок размера файла
static const uint32_t session_filesize_pos = sizeof(uint32_t);

//Блок размера структуры session_s
static const uint32_t session_size_pos = sizeof(uint32_t) * 2;

//Блок структуры session_s
static const uint32_t session_s_pos = sizeof(uint32_t) * 3;

//Блок границы, разделяющий session_s и KV
static const uint32_t session_boundary_middle_pos = sizeof(uint32_t) * 3 + sizeof(session_s);

//Первый блок [начало вложения KV]
static const uint32_t session_kvbegin_first_pos = sizeof(uint32_t) * 4 + sizeof(session_s);


/*
 * Позиционирование блоков в файле (от конца файла)
 */

//Последний блок границы
static const uint32_t session_boundary_last_pos = sizeof(uint32_t);

//Последний блок [окончание вложения KV]
static const uint32_t session_kvend_last_pos = sizeof(uint32_t) * 2;




/***********************************************************************
 * Функции
 **********************************************************************/

void			sessionEngineInit(void);	//Инициализация сессий, установка опций для сессий из конфигурации
inline const char *	sessionGetName(void);	//Возвращает имя переменной сессии
inline bool		sessionCacheEnabled(void);	//Возвращает состояние кеша сессий (кеш используется или не используется)
session_s *		sessionNew(const char * session_id);	//Создание новой сессии
inline void		sessionFree(session_s * session);		//Освобождение памяти, занятой сессией
int				sessionSaveToFile(session_s * session);		//Запись сессии в файл
bool			sessionCheckStructure(buffer_s * buf);	//Проверяет корректность структуры данных сессии, загруженной из файла
session_s *		sessionGetFromBuffer(buffer_s * buf);	//Чтение структуры сессии из буффера
bool			sessionIdIsGood(const char * ptr);		//Проверка корректности ID сессии
bool			sessionExpired(session_s * session);	//Проверяет актуальность сессии
session_s *		sessionLoadFromFile(const char * session_id);	//Чтение сессии из файла
session_s *		sessionStart(const char * session_id);	//Загрузка существующей сессии из файла сессии или старт новой сессии 
bool			sessionIsValidClient(session_s * session, socket_addr_s * sa, uint32_t user_agent);	//Проверяет сессию на принадлежность указанному пользователю

//Функции записи переменных в сессию
inline bool		sessionSetKV(session_s * session, kv_s * kv, const char * key_name, uint32_t key_len);
inline bool		sessionSetBool(session_s * session, const char * path, bool value);
inline bool		sessionSetInt(session_s * session, const char * path, int64_t value);
inline bool		sessionSetDouble(session_s * session, const char * path, double value);
inline bool		sessionSetString(session_s * session, const char * path, const char * str, uint32_t len);
inline bool		sessionSetStringPtr(session_s * session, const char * path, char * str, uint32_t len);
inline bool		sessionSetDatetime(session_s * session, const char * path, time_t ts, const char * format);

bool			sessionGetBool(session_s * session, const char * path, bool def);
int64_t			sessionGetInt(session_s * session, const char * path, int64_t def);
double			sessionGetDouble(session_s * session, const char * path, double def);
const char *	sessionGetStringPtr(session_s * session, const char * path, const char * def);
const_string_s * sessionGetStringSPtr(session_s * session, const char * path, const_string_s * def);
char *			sessionGetString(session_s * session, const char * path, uint32_t * olen);	//Возвращает текстовое значение переменной сессии
string_s * 		sessionGetStringS(session_s * session, const char * path);	//Возвращает текстовое значение переменной сессии в структуре string_s
buffer_s *		sessionGetBuffer(session_s * session, const char * path, buffer_s * buf);	//Возвращает текстовое значение переменной сессии в буфер buffer_s

bool			sessionFileDelete(const char * session_id);	//Удаление файла сессии

bool			sessionCacheSave(session_s * session);	//Добавляет сессию в кеш сессий
void			sessionCacheSaveAll(void);	//Сохранение на диск всех сессий, находящихся в кеше
session_s *		sessionCacheLoad(const char * session_id);	//Извлекает сессию из кеша сессий

void			sessionClose(session_s * session);		//Закрытие сессии

void			sessionDeleteExpired(void);	//Удаляет истекшие по времени действия файлы сессий

#ifdef __cplusplus
}
#endif

#endif //_XGSESSION_H
