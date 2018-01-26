/***********************************************************************
 * XG SERVER
 * core/db.h
 * Работа с базами данных
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/

#ifndef _XGDB_H
#define _XGDB_H

#ifdef __cplusplus
extern "C" {
#endif


/***********************************************************************
 * Подключаемые заголовки
 **********************************************************************/
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <mysql/errmsg.h>
#include <mysql/mysql.h>

#include "core.h"
#include "kv.h"


//Название основного соединения в базой данных, используемого фреймворком сервера
#define DB_INSTANCE_MAIN "main"

#define MYSQL_DATE_FORMAT "%Y-%m-%d"
#define MYSQL_TIME_FORMAT "%H:%M:%S"
#define MYSQL_DATETIME_FORMAT "%Y-%m-%d %H:%M:%S"



/***********************************************************************
 * Объявления и декларации
 **********************************************************************/

//Драйвер СУБД
typedef enum{
	DB_DRIVER_NONE = 0,	//Неизвестно
	DB_DRIVER_MYSQL,	//MySQL
	DB_DRIVER_PGSQL,	//PostgreSQL
	DB_DRIVER_MSSQL		//MS SQL
} db_driver_e;



//Статус соединения (описывает в каком состоянии в настоящий момент находится соединение)
typedef enum{
	DB_INSTANCE_NONE				= 0,	//В текущий момент данное соединение не используется
	DB_INSTANCE_CREATING			= 1,	//Идет создание экземпляра соединения
	DB_INSTANCE_CONNECTING			= 2,	//Идет установка соединения
	DB_INSTANCE_IDLE				= 3,	//Ожидание запроса
	DB_INSTANCE_DATA_INCOMMING		= 4,	//В результате запроса получены данные
	DB_INSTANCE_DATA_WORKING		= 5,	//Идет обработка полученных данных запроса
	DB_INSTANCE_DATA_COMPLETE		= 6,	//Обработаны все полученные данные
	DB_INSTANCE_DISCONNECTED		= 7		//Соединение разорвано или не установлено
} instance_state_e;


//Уровень изоляции транзакций
typedef enum{
	ISOLATION_LEVEL_DEFAULT,
	ISOLATION_LEVEL_REPEATABLE_READ,
	ISOLATION_LEVEL_READ_UNCOMMITTED,
	ISOLATION_LEVEL_READ_COMMTITED,
	ISOLATION_LEVEL_SERIALIZABLE
}mysql_isolation_level_e;


//Типы SQL запросов
typedef enum{
	SQL_QUERY_UNDEFINED = 0,	//Тип SQL запроса не определен
	SQL_QUERY_EMPTY,			//Пустой SQL запрос
	SQL_QUERY_ALTER,			//ALTER
	SQL_QUERY_COPY,				//COPY
	SQL_QUERY_CREATE,			//CREATE
	SQL_QUERY_COMMIT,			//COMMIT
	SQL_QUERY_DELETE,			//DELETE
	SQL_QUERY_DROP,				//DROP
	SQL_QUERY_GRANT,			//GRANT
	SQL_QUERY_INSERT,			//INSERT
	SQL_QUERY_LOAD,				//LOAD DATA
	SQL_QUERY_LOCK,				//LOCK
	SQL_QUERY_REPLACE,			//REPLACE
	SQL_QUERY_REVOKE,			//REVOKE
	SQL_QUERY_ROLLBACK,			//ROLLBACK
	SQL_QUERY_SELECT,			//SELECT
	SQL_QUERY_SET,				//SET
	SQL_QUERY_SHOW,				//SHOW
	SQL_QUERY_TRUNCATE,			//TRUNCATE
	SQL_QUERY_TRANSACTION,		//START TRANSACTION or BEGIN
	SQL_QUERY_UNLOCK,			//UNLOCK
	SQL_QUERY_UPDATE			//UPDATE
}sql_query_type_e;


//Модель получения данных при выборках (mysql_use_result() или mysql_store_result())
typedef enum{
	MYSQL_RESULT_NONE = 0,
	MYSQL_RESULT_USE,
	MYSQL_RESULT_STORE
}mysql_result_e;


//Тип отображения строки результатв выборки
typedef enum{
	ROWAS_ARRAY	= 0,
	ROWAS_OBJECT= 1
}rowas_e;


//Типы данных
typedef enum{
	DT_NULL 	= 0,		//NULL
	DT_BOOL		= 1,		//MYSQL_FIELD->type == MYSQL_TYPE_TINY && MYSQL_FIELD->length == 1 /* && MYSQL_FIELD->flag & UNSIGNED_FLAG */
	DT_INT32	= 2,
	DT_INT64	= 3,
	DT_DOUBLE	= 4,
	DT_STRING	= 5,
	DT_BINARY	= 6,
	DT_DATE		= 7,
	DT_TIME		= 8,
	DT_DATETIME	= 9,
	DT_TIMESTAMP= 10,
	DT_ENUM		= 11
} dt_t;




/***********************************************************************
 * Структуры
 **********************************************************************/

typedef struct		type_mysql_options_s	mysql_options_s;	//Настройки соединения с MySQL
typedef struct		type_mysql_s			mysql_s;			//Соединение с MySQL базой данных
typedef struct		type_bind_s				bind_s;				//Структура вставки в параметризованный запрос
typedef struct		type_sqltemplate_s		sqltemplate_s;		//Параметризованный запрос


//Структура полей в результатах 
typedef struct type_field_s{
	const char		* name;
	uint32_t		name_len;
	dt_t			type;
	
} field_s;


//Настройки соединения с MySQL
typedef struct type_mysql_options_s{
	const char		* username;
	const char		* password;
	const char		* database;
	const char		* host;
	const char		* unix_socket;
	bool			use_ssl;
	const char		* ssl_cert;
	const char		* ssl_key;
	const char		* ssl_ca;
	char			* ssl_ca_path;
	const char		* ssl_cipher;
	unsigned int	port;				//Порт соединения с сервером
	const char		* config_file;		//Читать параметры из указанного файла опций вместо my.cnf.
	const char		* config_group;		//Читать параметры из именованной группы из файла опций my.cnf или файла, определенного в config_file
	unsigned int	flags;				//Флаги соединения
} mysql_options_s;


//Структура соединения с MySQL базой данных
typedef struct type_mysql_s{
	pthread_t			thread_id;		//Дескриптор потока
	const char			* instance_name;//Имя экземпляра
	MYSQL 				* mysql;		//Экземпляр объекта MySQL
	mysql_options_s		* config;		//Настройки соединения
	instance_state_e	state;			//Текущий статус соединения
	mysql_s				* next;			//Следующее MySQL соединение
	bool				in_transaction;	//Признак, указывающий что сейчас выполняется транзакция
	MYSQL_RES			* result;		//Результаты последнего запроса
	MYSQL_FIELD			* mysql_fields;	//MySQL Поля в результатах выборики
	field_s				* fields;		//Информация о полях результата выборки
	MYSQL_ROW			data_row;		//Текущая строка результатов
	unsigned long		* data_lengths;	//Размеры данных строки результатов
	uint64_t			last_insert_id;	//Последний вставленный ID
	uint64_t			row_index;		//Номер обрабатываемой строки в выборке
	uint64_t			rows_count;		//Количество строк в результате последнего запроса
	uint64_t			fields_count;	//Количество полей в результате последнего запроса
	uint64_t			affected_rows;	//Количество строк измененных последним запросом
	sql_query_type_e	query_type;		//Тип последнего запроса
	mysql_result_e		result_model;	//Модель получения данных при выборках (mysql_use_result() или mysql_store_result())
	sqltemplate_s		* template;		//Параметризованный запрос
} mysql_s;



//Структура вставки в параметризованный запрос
typedef struct type_bind_s{
	bind_s		* next;		//Следующая вставка
	union{
		const char	* v_const;
		char		* v_char;
	};
	uint32_t	v_len;		//Длинна значения
	bool		is_const;	//Значение является константой и при удалении вставки free делать не надо
} bind_s;



//Структура параметризованного запроса
typedef struct type_sqltemplate_s{
	bind_s		* first;
	bind_s		* last;
	db_driver_e driver;
	union{
		const char	* query_const;
		char		* query_char;
	};
	uint32_t	query_len;
	uint32_t	binds_len;
	bool		is_const;	//Значение является константой и при удалении free делать не надо
	sqltemplate_s * next;	//для IDLE списка
} sqltemplate_s;


/*
//Структура элемента условия
typedef struct type_condition_s{
	condition_s *	next;
	char *			field_name;
	uint32_t		field_len;

} condition_s;
*/




/***********************************************************************
 * Функции: core/db.c - Работа с базами данных
 **********************************************************************/

void					dbInit(void);	//Инициализация механизмов работы с базами данных
void					dbEnd(void);	//Завершение работы с базами данных
mysql_options_s *		dbGetMysqlConfig(kv_s * instance);	//Получение настроек MySQL из конфигурации

sql_query_type_e	sqlQueryTypeDetect(const char * sql);	//нализирует SQL запрос и возвращает тип запроса
inline bool			sqlIsChange(sql_query_type_e type);	//Функция определяет, является ли SQL запрос изменяющим данные или нет
const char *		sqlQueryTypeString(sql_query_type_e type);	//Возвращает текстовое представление типа запроса
const char *		dtString(dt_t type);	//Возвращает текстовое представление типа dt_t

sqltemplate_s *	sqlTemplate(const char * sql_template, size_t template_len, bool is_const, db_driver_e driver);	//Задает SQL шаблон
void			sqlTemplateFree(sqltemplate_s * template);	//Удаление SQL шаблона
void			bindQueueFree(bind_s * bind);	//Удаление цепочки вставок
bind_s *		bindNew(sqltemplate_s * template);	//Добавляет новую вставку в параметризованный запрос
bind_s *		bindNull(sqltemplate_s * template);	//Добавляет вставку типа NULL
bind_s *		bindBool(sqltemplate_s * template, bool value);	//Добавляет вставку типа BOOL
bind_s *		bindInt(sqltemplate_s * template, int64_t value);	//Добавляет вставку типа INT
bind_s *		bindDouble(sqltemplate_s * template, double value, int ndigits);	//Добавляет вставку типа DOUBLE
bind_s *		bindString(sqltemplate_s * template, const char * value, size_t value_len);	//Добавляет вставку типа String
bind_s *		bindSql(sqltemplate_s * template, const char * value, size_t value_len, bool is_const);	//Добавляет вставку типа SQL
bind_s *		bindDate(sqltemplate_s * template, struct tm * tm);	//Добавляет вставку типа DATE
bind_s *		bindTime(sqltemplate_s * template, struct tm * tm);	//Добавляет вставку типа TIME
bind_s *		bindDatetime(sqltemplate_s * template, struct tm * tm);	//Добавляет вставку типа DATETIME
bind_s *		bindDateT(sqltemplate_s * template, time_t ts);	//Добавляет вставку типа DATE
bind_s *		bindTimeT(sqltemplate_s * template, time_t ts);	//Добавляет вставку типа TIME
bind_s *		bindDatetimeT(sqltemplate_s * template, time_t ts);	//Добавляет вставку типа DATETIME
char *			sqlTemplateParse(sqltemplate_s * template, uint32_t * olen);	//Парсит SQL шаблон и возвращает готовй SQL запрос



/***********************************************************************
 * Функции: core/db_mysql.c - Работа с базами данных MySQL
 **********************************************************************/

void		mysqlAddInstanceOptions(const char * instance_name, mysql_options_s * config);	//Добавляет настройки соединения в пул mysql_instances
void		mysqlInstanceOptionsFree(void * ptr);	//Удаление структуры настроек соединения
mysql_s *	mysqlGetInstance(mysql_s * db, const char * instance_name);	//Получение экземпляра соединения с базой данных MySQL
mysql_s *	mysqlCreateInstance(const char * instance_name, mysql_options_s * config);	//Создание соединения с базой данных MySQL
void		mysqlFreeInstance(mysql_s * instance);	//Закрывает и освобождает соединение с MySQL
mysql_s *	mysqlCreateAllInstances(void);	//Создает все соединения MySQL, настройки которых заданы в пуле mysql_instances
void		mysqlFreeAllInstances(void * ptr);	//Закрывает и освобождает все соединения с MySQL

mysql_s *	mysqlConnect(mysql_s * instance);	//Соединение с базой данных MySQL
inline bool	mysqlPing(mysql_s * instance);	//Проверяет работает или нет подключение. В случае неработоспособности будет предпринято автоматическое переподключение.
mysql_s *	mysqlClear(mysql_s * instance);	//"Обнудение" соединения - сброс всех данных и транзакций, полученных в предшествующем запросе

char *		mysqlEscape(const char * str, uint32_t ilen, bool quote, uint32_t * olen);	//Квотирует строку делая ее безопасной для SQL запроса
buffer_s *	mysqlEscapeBuffer(const char * str, uint32_t ilen, bool quote, buffer_s * buf);	//Квотирует строку делая ее безопасной для SQL запроса в буфер buffer_s

sqltemplate_s *	mysqlTemplate(mysql_s * instance, const char * sql_template, size_t template_len, bool is_const);	//Создает SQL шаблон для генерации запроса
inline bool	mysqlBindNull(mysql_s * instance);	//Добавляет вставку типа NULL
inline bool	mysqlBindBool(mysql_s * instance, bool value);	//Добавляет вставку типа BOOL
inline bool	mysqlBindInt(mysql_s * instance, int64_t value);	//Добавляет вставку типа INT
inline bool	mysqlBindDouble(mysql_s * instance, double value, int ndigits);	//Добавляет вставку типа DOUBLE
inline bool	mysqlBindString(mysql_s * instance, const char * value, size_t value_len);	//Добавляет вставку типа String
inline bool	mysqlBindSql(mysql_s * instance, const char * value, size_t value_len, bool is_const);	//Добавляет вставку типа SQL
inline bool	mysqlBindDate(mysql_s * instance, struct tm * tm);	//Добавляет вставку типа DATE
inline bool	mysqlBindTime(mysql_s * instance, struct tm * tm);	//Добавляет вставку типа TIME
inline bool	mysqlBindDatetime(mysql_s * instance, struct tm * tm);	//Добавляет вставку типа DATETIME
inline bool	mysqlBindDateT(mysql_s * instance, time_t ts);	//Добавляет вставку типа DATE
inline bool	mysqlBindTimeT(mysql_s * instance, time_t ts);	//Добавляет вставку типа TIME
inline bool	mysqlBindDatetimeT(mysql_s * instance, time_t ts);	//Добавляет вставку типа DATETIME
inline bool	mysqlBindKV(mysql_s * instance, kv_s * kv);	//Добавляет вставки из объекта KV
inline char * mysqlTemplateParse(mysql_s * instance, uint32_t * olen);	//Парсит SQL шаблон и возвращает готовй SQL запрос


bool		mysqlInTransaction(mysql_s * instance);	//Функция возвращает, установлена ли в настоящий момент транзакция
bool		mysqlTransaction(mysql_s * instance);	//Функция запускает транзакцию
bool		mysqlCommit(mysql_s * instance);	//Завершение транзакции - commit
bool		mysqlRollback(mysql_s * instance);	//Завершение транзакции - commit
bool		mysqlTransactionLevel(mysql_s * instance, mysql_isolation_level_e level);	//Установить уровень изоляции для транзакций

bool 		mysqlQuery(mysql_s * instance, const char * query, uint32_t query_len);	//Запрос к MySQL
bool		mysqlUseResult(mysql_s * instance);	//Инициализирует копию результатов выборки из MYSQL построчно
bool		mysqlStoreResult(mysql_s * instance);	//Возвращает полный набор результатов выборки из MYSQL
bool		mysqlFreeResult(mysql_s * instance);	//Очистка результатов запроса
MYSQL_ROW	mysqlFetchRow(mysql_s * instance);	//Возвращает следующую строку с данными выборки
int			mysqlFieldIndex(mysql_s * instance, const char * name);	//Возвращает индекс поля по его имени в результатах выборки

bool		mysqlAsBool(mysql_s * instance, int index, bool * value);	//Возвращает BOOL значение поля
bool		mysqlAsDouble(mysql_s * instance, int index, double * value);	//Возвращает DOUBLE значение поля
bool		mysqlAsInt32(mysql_s * instance, int index, int32_t * value);	//Возвращает INT32 значение поля
bool		mysqlAsUInt32(mysql_s * instance, int index, uint32_t * value);	//Возвращает UNSIGNED INT32 значение поля
bool		mysqlAsInt64(mysql_s * instance, int index, int64_t * value);	//Возвращает INT64 значение поля
bool		mysqlAsString(mysql_s * instance, int index, char ** value, uint32_t *olen);	//Возвращает текстовое значение поля
bool		mysqlAsStringN(mysql_s * instance, int index, char ** value, uint32_t ilen, uint32_t *olen);	//Возвращает первые N символов текстового значения поля
bool		mysqlAsJson(mysql_s * instance, int index, buffer_s * value);	//Возвращает текстовое значение поля в формате Json
bool		mysqlAsDate(mysql_s * instance, int index, struct tm * t);	//Возвращает DATE значение поля в структуру struct tm
bool		mysqlAsTime(mysql_s * instance, int index, struct tm * t);	//Возвращает TIME значение поля в структуру struct tm
bool		mysqlAsDatetime(mysql_s * instance, int index, struct tm * t);	//Возвращает DATETIME значение поля в структуру struct tm
kv_s *		mysqlAsKV(mysql_s * instance, int index, rowas_e rowas);	//Возвращает значение поля в структуре KV
kv_s *		mysqlRowAsKV(mysql_s * instance, rowas_e rowas, kv_s * parent);	//Возвращает объект KV из текущей строки результатов выборки
kv_s *		mysqlRowAsStringKV(mysql_s * instance, rowas_e rowas, kv_s * parent);	//Возвращает объект KV из текущей строки результатов выборки где каждый элемент представлен в виде текстовой строки
buffer_s *	mysqlRowAsJson(mysql_s * instance, rowas_e rowas, buffer_s * buf);	//Возвращает текущую строку выборки в формате JSON
buffer_s *	mysqlSelectAsJson(mysql_s * instance, const char * query, rowas_e rowas, buffer_s * buf);	//Делает SQL запрос к базе на выборку данных и возвращает результат в JSON
buffer_s *	mysqlSelectFieldAsJson(mysql_s * instance, const char * query, buffer_s * buf);	//Делает SQL запрос к базе на выборку данных и возвращает массив в формате JSON, состоящий из первого столбца каждой строки выборки
kv_s *		mysqlSelectByKey(mysql_s * instance, const char * query, int index, rowas_e rowas, bool value_as_array, kv_s * parent);	//Выборка данных в объект KV с ключем, получаемым из значений поля field
buffer_s *	mysqlSelectByKeyAsJson(mysql_s * instance, const char * query, int index, rowas_e rowas, bool value_as_array, buffer_s * buffer);	//Выборка данных в строку формата JSON с ключем, получаемым из значений поля field
kv_s * 		mysqlSelectRecord(mysql_s * instance, const char * query, size_t query_len, rowas_e rowas, kv_s * parent);	//Возвращает первую строку выборки в объекте KV, остальные строки игнорируются (SQL запросы рекомендуется строить с LIMIT 1)
buffer_s * 	mysqlSelectRecordAsJson(mysql_s * instance, const char * query, size_t query_len, rowas_e rowas, buffer_s * buffer);	//Возвращает первую строку выборки в формате JSON, остальные строки игнорируются (SQL запросы рекомендуется строить с LIMIT 1)
char *		mysqlResultAsString(mysql_s * instance, const char * query, size_t query_len, uint32_t * olen);	//Возвращает первый столбец первой строки выборки как STRING значение, все остальное игнорируется (SQL запросы рекомендуется строить с LIMIT 1)
buffer_s *	mysqlResultAsJson(mysql_s * instance, const char * query, size_t query_len, buffer_s * buf);	//Возвращает первый столбец первой строки выборки как JSON значение, все остальное игнорируется (SQL запросы рекомендуется строить с LIMIT 1)
bool		mysqlResultAsBool(mysql_s * instance, const char * query, size_t query_len, bool * ok);	//Возвращает первый столбец первой строки выборки как Bool значение, все остальное игнорируется (SQL запросы рекомендуется строить с LIMIT 1)
bool		mysqlResultAsDouble(mysql_s * instance, const char * query, size_t query_len, double * value);	//Возвращает первый столбец первой строки выборки как Double значение, все остальное игнорируется (SQL запросы рекомендуется строить с LIMIT 1)
bool		mysqlResultAsInt32(mysql_s * instance, const char * query, size_t query_len, int32_t * value);	//Возвращает первый столбец первой строки выборки как Int32 значение, все остальное игнорируется
bool		mysqlResultAsInt64(mysql_s * instance, const char * query, size_t query_len, int64_t * value);	//Возвращает первый столбец первой строки выборки как Int64 значение, все остальное игнорируется
inline bool	mysqlSelect(mysql_s * instance, const char * query, size_t query_len);	//Запрос SELECT
inline bool	mysqlInsert(mysql_s * instance, const char * table, kv_s * fields, kv_s * defaults);	//Запрос INSERT
inline bool	mysqlUpdate(mysql_s * instance, const char * table, kv_s * conditions, kv_s * fields, kv_s * defaults);	//Обновление записи в таблице базы данных

buffer_s *	mysqlConditions(kv_s * conditions, const char * separator, const char * prefix, buffer_s * buf);	//Построение части SQL запроса на основании данных массива условий
#ifdef __cplusplus
}
#endif

#endif //_XGDB_H
