/***********************************************************************
 * XG SERVER
 * core/db_mysql.c
 * Работа с базой данных MySQL
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include <mysql/errmsg.h>
#include <mysql/mysql.h>
#include "core.h"
#include "db.h"
#include "kv.h"
#include "globals.h"


//Настройки соединений с базами данных из конфигурации
static kv_s * mysql_instances			= NULL;
static uint32_t mysql_instances_count	= 0;



/***********************************************************************
 * Экземпляры соединений
 **********************************************************************/ 

//Добавляет настройки соединения в пул mysql_instances
void
mysqlAddInstanceOptions(const char * instance_name, mysql_options_s * config){
	if(mysql_instances_count > 3){
		mysqlInstanceOptionsFree(config);
		DEBUG_MSG("Warning: found more than 3 instances of MySQL in config, instance [%s] ignored", instance_name);
		return;
	}
	if(!mysql_instances) mysql_instances = kvNewRoot();
	kvAppendPointer(mysql_instances, instance_name, config, mysqlInstanceOptionsFree, KV_REPLACE);

	DEBUG_MSG("Mysql database instance [%s] added to mysql_instances pool", instance_name);

	mysql_instances_count++;
}//END: mysqlAddInstance



/*
 * Удаление структуры настроек соединения
 */
void
mysqlInstanceOptionsFree(void * ptr){
	mysql_options_s * config = (mysql_options_s *)ptr;
	if(config->ssl_ca_path) mFree(config->ssl_ca_path);
	mFree(config);
}//END: mysqlInstanceOptionsFree



/*
 * Получение экземпляра соединения с базой данных MySQL
 */
mysql_s *
mysqlGetInstance(mysql_s * instance, const char * instance_name){
	if(!instance) return NULL;
	for(; instance != NULL; instance = instance->next){
		if(stringCompareCase(instance->instance_name, instance_name)) return mysqlClear(instance);
	}
	return NULL;
}//END: mysqlGetInstance



/*
 * Создание экземпляра соединения с базой данных MySQL
 */
mysql_s *
mysqlCreateInstance(const char * instance_name, mysql_options_s * config){
	if(!config) return NULL;
	mysql_s * instance = (mysql_s *)mNewZ(sizeof(mysql_s));
	instance->config = config;
	instance->state = DB_INSTANCE_CREATING;
	instance->instance_name = instance_name;
	instance->thread_id = pthread_self();
	if((instance->mysql = mysql_init(NULL))== NULL) FATAL_ERROR("mysql_init() return NULL");
	if (config->config_file != NULL){
		mysql_options(instance->mysql, MYSQL_READ_DEFAULT_FILE, config->config_file);
	}
	mysql_options(instance->mysql, MYSQL_READ_DEFAULT_GROUP, (config->config_group != NULL ? config->config_group : "client"));

	my_bool opt_true = 1;
	my_bool opt_false = 0;
	mysql_options(instance->mysql, MYSQL_OPT_RECONNECT, &opt_true);
	mysql_options(instance->mysql, MYSQL_SET_CHARSET_NAME, "utf8");
	//unsigned int timeout = 86400;
	//mysql_options(instance->mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
	mysql_options(instance->mysql, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &opt_false); 
	/*mysql_options(instance->mysql, MYSQL_INIT_COMMAND,"SET autocommit=0");*/

	if(config->use_ssl){
		mysql_ssl_set(instance->mysql, config->ssl_key, config->ssl_cert, config->ssl_ca, config->ssl_ca_path, config->ssl_cipher);
	}

	DEBUG_MSG("Create in Thread ID [%d] MySQL instance: %s", (int)pthread_self(), instance->instance_name);

	return instance;
}//END: mysqlCreateInstance


/*
 * Закрывает и освобождает соединение с MySQL
 */
void
mysqlFreeInstance(mysql_s * instance){
	mysqlClear(instance);
	if(instance->mysql) mysql_close(instance->mysql);
	mFree(instance);
}//END: mysqlFreeInstance




/*
 * Создает все соединения MySQL, настройки которых заданы в пуле mysql_instances
 */
mysql_s *
mysqlCreateAllInstances(void){
	if(!mysql_instances_count) return NULL;
	kv_s * kv_config;
	mysql_s * instances = NULL;
	mysql_s * instance = NULL;
	mysql_options_s * config;
	for(kv_config = mysql_instances->value.v_list.first; kv_config != NULL; kv_config = kv_config->next){
		config = (mysql_options_s *)kv_config->value.v_pointer.ptr;
		instance = mysqlCreateInstance(kv_config->key_name, config);
		if(instance){
			if(instances != NULL){
				instance->next = instances;
			}
			instances = instance;
			mysqlConnect(instance);
		}
	}
	return instances;
}//END: mysqlCreateAllInstances


/*
 * Закрывает и освобождает все соединения с MySQL
 */
void
mysqlFreeAllInstances(void * ptr){
	mysql_s * instances = (mysql_s *)ptr;
	mysql_s * instance = instances;
	mysql_s * current;
	while(instance){
		current = instance;
		instance = instance->next;
		mysqlFreeInstance(current);
	}
}//END: mysqlFreeAllInstances





/***********************************************************************
 * Функции
 **********************************************************************/ 


/*
 * Соединение с базой данных MySQL
 */
mysql_s *
mysqlConnect(mysql_s * instance){

	if(!instance || !instance->mysql) RETURN_ERROR(NULL, "!instance || !instance->mysql");
	instance->state = DB_INSTANCE_CONNECTING;

	//Установка соединения с базой данных
	bool failed = mysql_real_connect(
		instance->mysql, 
		instance->config->host,
		instance->config->username,
		instance->config->password,
		instance->config->database,
		instance->config->port,
		instance->config->unix_socket,
		instance->config->flags
	) == NULL;

	if(failed){
		instance->state = DB_INSTANCE_DISCONNECTED;
		RETURN_ERROR(NULL, "%s: Connect failed to database [%s]: %s", instance->instance_name, instance->config->database, mysql_error(instance->mysql));
	}

	my_bool opt_true = 1;
	mysql_options(instance->mysql, MYSQL_OPT_RECONNECT, &opt_true); 
	/*
	my_bool opt_false = 0;
	mysql_autocommit(instance->mysql, opt_false);
	*/

	instance->state = DB_INSTANCE_IDLE;

	if(!mysqlQuery(instance, "SET NAMES `utf8`", 16)) RETURN_ERROR(NULL, "SET NAMES `utf8`");
	if(!mysqlQuery(instance, "SET CHARACTER SET `utf8`", 24)) RETURN_ERROR(NULL, "SET CHARACTER SET `utf8`");
	if(!mysqlTransactionLevel(instance, ISOLATION_LEVEL_REPEATABLE_READ)) RETURN_ERROR(NULL, "mysqlTransactionLevel(ISOLATION_LEVEL_REPEATABLE_READ)");

	DEBUG_MSG("%s: Successfully connected to database [%s] %s", instance->instance_name, instance->config->database, (instance->config->use_ssl ? " via SSL" : ""));

	return instance;
}//END: mysqlConnect



/*
 * Проверяет работает или нет подключение. В случае неработоспособности будет предпринято автоматическое переподключение.
 */
inline bool
mysqlPing(mysql_s * instance){
	if (instance->state == DB_INSTANCE_DISCONNECTED){
		if(!mysqlConnect(instance)) return false;
	}
	if(mysql_ping(instance->mysql)!=0){
		DEBUG_MSG("%s: mysql_ping error: %s", instance->instance_name, mysql_error(instance->mysql));
		if(!mysqlConnect(instance)){
			instance->state = DB_INSTANCE_DISCONNECTED;
			return false;
		}
	}
	return true;
}//END: mysqlPing



/*
 * "Обнудение" соединения - сброс всех данных и транзакций, полученных в предшествующем запросе
 */
mysql_s *
mysqlClear(mysql_s * instance){
	//Если экземпляр соединения существует
	if(instance->mysql){
		//Если соединение установлено
		if(instance->state != DB_INSTANCE_DISCONNECTED){
			//Если сейчас выполняется транзакция
			if(instance->in_transaction) mysqlRollback(instance);
		}
	}
	mysqlFreeResult(instance);
	instance->in_transaction = false;

	return instance;
}//END: mysqlClear





/***********************************************************************
 * Типы данных, данные
 **********************************************************************/ 

/*
 * Возвращает внутренний тип данных исходя из типа MYSQL 
 */
static dt_t
_mysqlDataType(enum enum_field_types type, int flags, unsigned int length){
	switch (type){
		case MYSQL_TYPE_VAR_STRING:
		case MYSQL_TYPE_STRING:
			return DT_STRING;

		case MYSQL_TYPE_TINY: 
		case MYSQL_TYPE_YEAR:
		case MYSQL_TYPE_SHORT:
		case MYSQL_TYPE_INT24:
		case MYSQL_TYPE_LONG:
			return (length == 1 ? DT_BOOL : DT_INT32);

		case MYSQL_TYPE_LONGLONG:
			return DT_INT64;

		case MYSQL_TYPE_DECIMAL: 
		case MYSQL_TYPE_FLOAT:
		case MYSQL_TYPE_DOUBLE: 
		case 246: // 5.0 MYSQL_NEW_DECIMAL
			return DT_DOUBLE;

		case MYSQL_TYPE_TINY_BLOB:
		case MYSQL_TYPE_MEDIUM_BLOB:
		case MYSQL_TYPE_LONG_BLOB:
		case MYSQL_TYPE_BLOB:
			return (BIT_ISSET(flags,BINARY_FLAG) ? DT_BINARY : DT_STRING);

		case MYSQL_TYPE_DATE:
		case MYSQL_TYPE_NEWDATE:
			return DT_DATE;

		case MYSQL_TYPE_TIME:
			return DT_TIME;

		case MYSQL_TYPE_DATETIME:
		case MYSQL_TYPE_TIMESTAMP:
			return DT_DATETIME;

		case MYSQL_TYPE_ENUM:
		case MYSQL_TYPE_SET:
			return DT_ENUM;

		case MYSQL_TYPE_NULL:
			return DT_NULL;

		default:
			return (BIT_ISSET(flags,BINARY_FLAG) ? DT_BINARY : DT_STRING);
	}
}//END: _mysqlDataType




/*
 * Квотирует строку делая ее безопасной для SQL запроса
 */
char *
mysqlEscape(const char * str, uint32_t ilen, bool quote, uint32_t * olen){

#define _add(ch) \
do{ \
	*rptr++ = ch;\
	index++;\
}while(0)

	if(!str) return NULL;
	register uint32_t index = 0;
	register uint32_t n = 0;
	register const char * ptr = str;
	uint32_t allocated = (ilen > 0 ? ilen * 2 + (quote ? 2 : 0 ) : buffer_s_default_increment);
	char * result = mNew(allocated + 1);
	register char * rptr = result;
	if(quote) _add('"');
	while(*ptr || n < ilen){
		if(index >= allocated){
			allocated += buffer_s_default_increment;
			result = mResize(result, allocated + 1);
			rptr = &result[index];
		}
		switch(*ptr){
			case '\0': _add('\\'); _add('0'); break;
			case '\r': _add('\\'); _add('r'); break;
			case '\n': _add('\\'); _add('n'); break;
			case 0x1a: _add('\\'); _add('Z'); break;
			case '\'': _add('\\'); _add('\''); break;
			case '\"': _add('\\'); _add('\"'); break;
			case '\\': _add('\\'); _add('\\'); break;
			default:
				_add(*ptr);
		}
		ptr++;
		n++;
	}
	if(quote) _add('"');
	*rptr = '\0';
	if(olen)*olen = index;
	return result;
#undef _add
}//END: mysqlEscape



/*
 * Квотирует строку делая ее безопасной для SQL запроса в буфер buffer_s
 */
buffer_s *
mysqlEscapeBuffer(const char * str, uint32_t ilen, bool quote, buffer_s * buf){

#define _add(ch) bufferAddChar(buf, ch)

	if(!str) return buf;
	register const char * ptr = str;
	uint32_t allocated = (ilen > 0 ? ilen * 2 + (quote ? 2 : 0 ) : buffer_s_default_increment);

	if(!buf) buf = bufferCreate(allocated);

	if(quote) _add('"');
	while(*ptr){
		switch(*ptr){
			case '\0': _add('\\'); _add('0'); break;
			case '\r': _add('\\'); _add('r'); break;
			case '\n': _add('\\'); _add('n'); break;
			case 0x1a: _add('\\'); _add('Z'); break;
			case '\'': _add('\\'); _add('\''); break;
			case '\"': _add('\\'); _add('\"'); break;
			case '\\': _add('\\'); _add('\\'); break;
			default:
				_add(*ptr);
		}
		ptr++;
	}//while
	if(quote) _add('"');
	return buf;
#undef _add
}//END: mysqlEscapeBuffer



/***********************************************************************
 * Транзакции
 **********************************************************************/ 

/*
 * Функция возвращает, установлена ли в настоящий момент транзакция
 */
bool
mysqlInTransaction(mysql_s * instance){
	return instance->in_transaction;
}//END: mysqlInTransaction



/*
 * Функция запускает транзакцию
 */
bool
mysqlTransaction(mysql_s * instance){
	if(instance->in_transaction) RETURN_ERROR(true, "%s: Opening a new transaction with an already opened and Uncommitted transaction", instance->instance_name);
	if(!mysqlQuery(instance, "START TRANSACTION", 17)) RETURN_ERROR(false, "%s: Can not start a new transaction", instance->instance_name);
	return true;
}//END: mysqlTransaction



/*
 * Завершение транзакции - commit
 */
bool
mysqlCommit(mysql_s * instance){
	if(!instance->in_transaction) RETURN_ERROR(false, "%s: COMMIT call in the absence of transaction", instance->instance_name);
	if(!mysqlQuery(instance, "COMMIT", 6)) RETURN_ERROR(false, "%s: Can not COMMIT transaction", instance->instance_name);
	return true;
}//END: mysqlCommit



/*
 * Завершение транзакции - commit
 */
bool
mysqlRollback(mysql_s * instance){
	if(!instance->in_transaction) RETURN_ERROR(false, "%s: ROLLBACK call in the absence of transaction", instance->instance_name);
	if(!mysqlQuery(instance, "ROLLBACK", 8)) RETURN_ERROR(false, "%s: Can not ROLLBACK transaction", instance->instance_name);
	return true;
}//END: mysqlRollback



/*
 * Установить уровень изоляции для транзакций
 */
bool
mysqlTransactionLevel(mysql_s * instance, mysql_isolation_level_e level){
	if(instance->in_transaction) RETURN_ERROR(false, "%s: Attempt to set the transaction isolation level in an open transaction", instance->instance_name);
	const char * query;
	switch(level){
		case ISOLATION_LEVEL_READ_UNCOMMITTED	: query = "SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED"; break;
		case ISOLATION_LEVEL_READ_COMMTITED		: query = "SET TRANSACTION ISOLATION LEVEL READ COMMTITED"; break;
		case ISOLATION_LEVEL_SERIALIZABLE		: query = "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE"; break;
		case ISOLATION_LEVEL_REPEATABLE_READ	:
		case ISOLATION_LEVEL_DEFAULT			:
		default									: query = "SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED";
	}
	return mysqlQuery(instance, query, 0);
}//END: mysqlTransactionLevel





/***********************************************************************
 * Параметризованный запрос
 **********************************************************************/ 

/*
 * Создает SQL шаблон для генерации запроса
 */
sqltemplate_s *
mysqlTemplate(mysql_s * instance, const char * sql_template, size_t template_len, bool is_const){
	if(instance->template) sqlTemplateFree(instance->template);
	instance->template = sqlTemplate(sql_template, template_len, is_const, DB_DRIVER_MYSQL);
	return instance->template;
}//END: mysqlTemplate


//Добавляет вставку типа NULL
inline bool
mysqlBindNull(mysql_s * instance){
	if(!instance || !instance->template) return false; 
	bindNull(instance->template); 
	return true;
}//mysqlBindNull


//Добавляет вставку типа BOOL
inline bool
mysqlBindBool(mysql_s * instance, bool value){
	if(!instance || !instance->template) return false; 
	bindBool(instance->template, value); 
	return true;
}//mysqlBindBool


//Добавляет вставку типа INT
inline bool
mysqlBindInt(mysql_s * instance, int64_t value){
	if(!instance || !instance->template) return false; 
	bindInt(instance->template, value); 
	return true;
}//mysqlBindInt


//Добавляет вставку типа DOUBLE
inline bool
mysqlBindDouble(mysql_s * instance, double value, int ndigits){
	if(!instance || !instance->template) return false; 
	bindDouble(instance->template, value, ndigits); 
	return true;
}//mysqlBindDouble


//Добавляет вставку типа STRING
inline bool
mysqlBindString(mysql_s * instance, const char * value, size_t value_len){
	if(!instance || !instance->template) return false; 
	bindString(instance->template, value, value_len); 
	return true;
}//mysqlBindString


//Добавляет вставку типа SQL
inline bool
mysqlBindSql(mysql_s * instance, const char * value, size_t value_len, bool is_const){
	if(!instance || !instance->template) return false; 
	bindSql(instance->template, value, value_len, is_const); 
	return true;
}//mysqlBindSql


//Добавляет вставку типа DATE
inline bool
mysqlBindDate(mysql_s * instance, struct tm * tm){
	if(!instance || !instance->template) return false; 
	bindDate(instance->template, tm); 
	return true;
}//mysqlBindDate


//Добавляет вставку типа TIME
inline bool
mysqlBindTime(mysql_s * instance, struct tm * tm){
	if(!instance || !instance->template) return false; 
	bindTime(instance->template, tm); 
	return true;
}//mysqlBindTime


//Добавляет вставку типа DATETIME
inline bool
mysqlBindDatetime(mysql_s * instance, struct tm * tm){
	if(!instance || !instance->template) return false; 
	bindDatetime(instance->template, tm); 
	return true;
}//mysqlBindDatetime


//Добавляет вставку типа DATE
inline bool
mysqlBindDateT(mysql_s * instance, time_t ts){
	if(!instance || !instance->template) return false; 
	bindDateT(instance->template, ts); 
	return true;
}//mysqlBindDateT


//Добавляет вставку типа TIME
inline bool
mysqlBindTimeT(mysql_s * instance, time_t ts){
	if(!instance || !instance->template) return false; 
	bindTimeT(instance->template, ts); 
	return true;
}//mysqlBindTimeT


//Добавляет вставку типа DATETIME
inline bool
mysqlBindDatetimeT(mysql_s * instance, time_t ts){
	if(!instance || !instance->template) return false; 
	bindDatetimeT(instance->template, ts); 
	return true;
}//mysqlBindDatetimeT


//Добавляет вставки из объекта KV
inline bool
mysqlBindKV(mysql_s * instance, kv_s * kv){
	kv_s * node;
	for(node = kv->value.v_list.first; node != NULL; node = node->next){
		switch(node->type){
			case KV_NULL: mysqlBindNull(instance); break;
			case KV_BOOL: mysqlBindBool(instance, node->value.v_bool); break;
			case KV_INT: mysqlBindInt(instance, node->value.v_int); break;
			case KV_DOUBLE: mysqlBindDouble(instance, node->value.v_double, 6); break;
			case KV_DATETIME: mysqlBindDatetimeT(instance, node->value.v_datetime.ts); break;
			case KV_STRING:
			case KV_JSON: 
				mysqlBindString(instance, node->value.v_string.ptr, node->value.v_string.len);
			break;
			default:
			break;
		}
	}
	return true;
}//mysqlBindKV



/*
 * Парсит SQL шаблон и возвращает готовй SQL запрос
 */
inline char *
mysqlTemplateParse(mysql_s * instance, uint32_t * olen){
	if(!instance || !instance->template) return NULL; 
	return sqlTemplateParse(instance->template, olen);
}//mysqlTemplateParse





/***********************************************************************
 * Запрос в базу данных
 **********************************************************************/ 

/*
 * Запрос к MySQL
 */
bool 
mysqlQuery(mysql_s * instance, const char * query, uint32_t query_len){

	//Очистка результатов предыдущего запроса
	mysqlFreeResult(instance);

	if(!instance || !instance->mysql || instance->state != DB_INSTANCE_IDLE) return false;
	char * template_query = NULL;
	if(!query){
		if(!instance->template) return false;
		template_query = mysqlTemplateParse(instance, &query_len);
		if(!template_query) return false;
		query = (const char *)template_query;
		
	}else{
		if(!query_len) query_len = strlen(query);
	}

	//Удаление заданного SQL шаблона
	if(instance->template){
		sqlTemplateFree(instance->template);
		instance->template = NULL;
	}

	instance->query_type = sqlQueryTypeDetect(query);
	if(instance->query_type == SQL_QUERY_UNDEFINED || instance->query_type == SQL_QUERY_EMPTY) RETURN_ERROR(false,"%s: undefined query type or empty query: [[%s]]",instance->instance_name, query);

	//printf("SQL: %s\n",query);

	if(!mysqlPing(instance)) return false;
	if(mysql_real_query(instance->mysql, query, query_len) != 0){
		switch (mysql_errno(instance->mysql)){
		case CR_SERVER_GONE_ERROR:
		case CR_SERVER_LOST:
			instance->state = DB_INSTANCE_DISCONNECTED;
			break;
		default:
			break;
		}
		DEBUG_MSG(
			"%s: mysql_query error: %s\n"\
			"SQL:-------------------------------------\n"\
			"[[%s]]"\
			"\n-------------------------------------\n", 
			instance->instance_name, 
			mysql_error(instance->mysql), 
			query
		);
		if(template_query) mFree(template_query);
		return false;
	}//mysql_real_query
/*
	DEBUG_MSG(
		"%s: successfully query (type is %s)\n"\
		"SQL:-------------------------------------\n"\
		"[[%s]]"\
		"\n-------------------------------------\n", 
		instance->instance_name, 
		sqlQueryTypeString(instance->query_type), 
		query
	);
*/
	//Получены данные выборки
	if(mysql_field_count(instance->mysql) > 0){
		instance->state = DB_INSTANCE_DATA_INCOMMING;
	}
	//Данных в выборке нет
	else{
		instance->affected_rows = mysql_affected_rows(instance->mysql);
		instance->state = DB_INSTANCE_DATA_COMPLETE;

		switch(instance->query_type){

			//insert
			case SQL_QUERY_INSERT:
				instance->last_insert_id = (uint64_t)mysql_insert_id(instance->mysql);
			break;

			//transaction
			case SQL_QUERY_TRANSACTION:
				instance->in_transaction = true;
			break;

			//commit, rollback
			case SQL_QUERY_COMMIT:
			case SQL_QUERY_ROLLBACK:
				instance->in_transaction = false;
			break;

			default:
			break;
		}//switch

	}//Данных в выборке нет

	if(template_query) mFree(template_query);
	return true;
}//END: mysqlQuery



/*
 * Вычисляет информацию о полях результата выборки
 */
static void
_mysqlFieldsInfo(mysql_s * instance){
	instance->fields_count = (uint64_t)mysql_num_fields(instance->result);
	instance->mysql_fields = mysql_fetch_fields(instance->result);
	if(instance->fields) mFree(instance->fields);
	instance->fields = mCalloc(instance->fields_count, sizeof(field_s));
	register int i;
	for(i = 0; i < instance->fields_count; i++){
		instance->fields[i].name		= instance->mysql_fields[i].name;
		instance->fields[i].name_len	= instance->mysql_fields[i].name_length;
		instance->fields[i].type = _mysqlDataType(instance->mysql_fields[i].type, instance->mysql_fields[i].flags, instance->mysql_fields[i].length);
	}
}//END: _mysqlFieldsInfo



/*
 * Инициализирует копию результатов выборки из MYSQL построчно
 */
bool
mysqlUseResult(mysql_s * instance){
	if(instance->state != DB_INSTANCE_DATA_INCOMMING) return false;
	if(!(instance->result = mysql_use_result(instance->mysql))) RETURN_ERROR(false, "%s: mysql_use_result() fail: %s", instance->instance_name, mysql_error(instance->mysql));
	instance->rows_count = 0;
	instance->result_model = MYSQL_RESULT_USE;
	instance->state = DB_INSTANCE_DATA_WORKING;
	_mysqlFieldsInfo(instance);
	return true;
}//END: mysqlUseResult



/*
 * Возвращает полный набор результатов выборки из MYSQL
 */
bool
mysqlStoreResult(mysql_s * instance){
	if(instance->state != DB_INSTANCE_DATA_INCOMMING) return false;
	if(!(instance->result = mysql_store_result(instance->mysql))) RETURN_ERROR(false, "%s: mysql_store_result() fail: %s", instance->instance_name, mysql_error(instance->mysql));
	instance->rows_count = (uint64_t)mysql_num_rows(instance->result);
	instance->result_model = MYSQL_RESULT_STORE;
	instance->state = DB_INSTANCE_DATA_WORKING;
	_mysqlFieldsInfo(instance);
	return true;
}//END: mysqlStoreResult




/*
 * Сичтывает все данные, до конца набора при использовании mysql_use_result()
 */
static void
mysqlUseResultEof(mysql_s * instance){
	if(instance->state != DB_INSTANCE_DATA_WORKING) return;
	MYSQL_ROW row;
	while((row = mysql_fetch_row(instance->result))) instance->row_index++;
	//mysql_fetch_row() потерпел неудачу из-за ошибки
	if(!mysql_eof(instance->result)){
		DEBUG_MSG("%s: mysqlUseResultEof() fail: %s", instance->instance_name, mysql_error(instance->mysql));
	}else{
		instance->rows_count	= (uint64_t)mysql_num_rows(instance->result);
		instance->state			= DB_INSTANCE_DATA_COMPLETE;
	}
}//END: mysqlUseResultEof



/*
 * Очистка результатов запроса
 */
bool
mysqlFreeResult(mysql_s * instance){
	if(instance->state == DB_INSTANCE_DATA_INCOMMING) mysqlUseResult(instance); 
	if(instance->result){
		//Если данные предыдущего запроса считывались при помощи mysql_use_result(),
		//то необходимо убедиться, что данные считаны все
		if(instance->result_model == MYSQL_RESULT_USE && instance->state != DB_INSTANCE_DATA_COMPLETE) mysqlUseResultEof(instance);
		mysql_free_result(instance->result);
	}
	if(instance->fields) mFree(instance->fields);
	instance->result			= NULL;
	instance->mysql_fields		= NULL;
	instance->data_row			= NULL;
	instance->data_lengths		= NULL;
	instance->fields			= NULL;
	instance->fields_count		= 0;
	instance->rows_count		= 0;
	instance->row_index			= 0;
	instance->affected_rows		= 0;
	instance->last_insert_id	= 0;
	instance->result_model		= MYSQL_RESULT_NONE;
	instance->state				= DB_INSTANCE_IDLE;
	return true;
}//END: mysqlFreeResult



/*
 * Возвращает следующую строку с данными выборки
 */
MYSQL_ROW
mysqlFetchRow(mysql_s * instance){
	if(instance->state != DB_INSTANCE_DATA_WORKING) return NULL;
	instance->data_row = mysql_fetch_row(instance->result);
	if(!instance->data_row){
		if(!mysql_eof(instance->result)) RETURN_ERROR(NULL, "%s: mysql_fetch_row() fail: %s", instance->instance_name, mysql_error(instance->mysql));
		instance->state		= DB_INSTANCE_DATA_COMPLETE;
		instance->rows_count= (uint64_t)mysql_num_rows(instance->result);
		mysqlFreeResult(instance);
		return NULL;
	}
	instance->data_lengths = mysql_fetch_lengths(instance->result);
	instance->row_index++;
	return instance->data_row;
}//mysqlGetRow



/*
 * Возвращает индекс поля по его имени в результатах выборки 
 */
int
mysqlFieldIndex(mysql_s * instance, const char * name){
	if(!instance->result || !instance->fields || !name) return -1;
	register int i;
	for(i = 0; i < instance->fields_count; i++){
		if(stringCompareCase(name, instance->fields[i].name)) return i;
	}
	return -1;
}//END: mysqlFieldIndex 



#define _mysql_data_check(idx) do{ \
	if(instance->state != DB_INSTANCE_DATA_WORKING || idx < 0 || idx >= instance->fields_count || !instance->data_row) return def; \
}while(0) 



/*
 * Возвращает BOOL значение поля
 */
bool
mysqlAsBool(mysql_s * instance, int index, bool * value){
	bool def = false;
	_mysql_data_check(index);
	if(instance->fields[index].type != DT_BOOL) return false;
	char * ptr = instance->data_row[index];
	if(value) *value = (*ptr != '0' ? true : false);
	return true;
}//END: mysqlAsBool



/*
 * Возвращает DOUBLE значение поля
 */
bool
mysqlAsDouble(mysql_s * instance, int index, double * value){
	bool def = false;
	_mysql_data_check(index);
	if(instance->fields[index].type != DT_INT32 && instance->fields[index].type != DT_INT64 && instance->fields[index].type != DT_BOOL && instance->fields[index].type != DT_DOUBLE) return false;
	if(value) *value = (double)atof( instance->data_row[index] );
	return true;
}//END: mysqlAsDouble



/*
 * Возвращает INT32 значение поля
 */
bool
mysqlAsInt32(mysql_s * instance, int index, int32_t * value){
	bool def = false;
	_mysql_data_check(index);
	//if(instance->fields[index].type != DT_INT32 && instance->fields[index].type != DT_INT64 && instance->fields[index].type != DT_BOOL) return false;
	if(value) *value = (int32_t)atoi(instance->data_row[index]);
	return true;
}//END: mysqlAsInt32



/*
 * Возвращает INT32 значение поля
 */
bool
mysqlAsUInt32(mysql_s * instance, int index, uint32_t * value){
	bool def = false;
	_mysql_data_check(index);
	//if(instance->fields[index].type != DT_INT32 && instance->fields[index].type != DT_INT64 && instance->fields[index].type != DT_BOOL) return false;
	if(value) *value = (uint32_t)atol(instance->data_row[index]);
	return true;
}//END: mysqlAsUInt32



/*
 * Возвращает INT64 значение поля
 */
bool
mysqlAsInt64(mysql_s * instance, int index, int64_t * value){
	bool def = false;
	_mysql_data_check(index);
	//if(instance->fields[index].type != DT_INT32 && instance->fields[index].type != DT_INT64 && instance->fields[index].type != DT_BOOL) return false;
	if(value) *value = (int64_t)stringToInt64(instance->data_row[index], NULL);
	return true;
}//END: mysqlAsInt64



/*
 * Возвращает текстовое значение поля
 */
bool
mysqlAsString(mysql_s * instance, int index, char ** value, uint32_t *olen){
	bool def = false;
	_mysql_data_check(index);
	if(value) *value = stringCloneN(instance->data_row[index], instance->data_lengths[index], olen);
	return true;
}//END: mysqlAsString



/*
 * Возвращает первые N символов текстового значения поля
 */
bool
mysqlAsStringN(mysql_s * instance, int index, char ** value, uint32_t ilen, uint32_t *olen){
	bool def = false;
	_mysql_data_check(index);
	if(value) *value = stringCloneN(instance->data_row[index], min(ilen, instance->data_lengths[index]), olen);
	return true;
}//END: mysqlAsStringN




/*
 * Возвращает текстовое значение поля в формате Json
 */
bool
mysqlAsJson(mysql_s * instance, int index, buffer_s * value){
	bool def = false;
	_mysql_data_check(index);
	if(value){
		encodeJson(instance->data_row[index], (uint32_t)instance->data_lengths[index], value);
	}
	return true;
}//END: mysqlAsJson



/*
 * Возвращает DATE значение поля в структуру struct tm
 */
bool
mysqlAsDate(mysql_s * instance, int index, struct tm * t){
	bool def = false;
	_mysql_data_check(index);
	memset(t, '\0', sizeof(struct tm));
	if(instance->fields[index].type == DT_DATE){
		sscanf(instance->data_row[index],"%04d-%02d-%02d", &t->tm_year, &t->tm_mon, &t->tm_mday);
	}else if(instance->fields[index].type == DT_DATETIME){
		sscanf(instance->data_row[index], "%04d-%02d-%02d %02d:%02d:%02d", &t->tm_year, &t->tm_mon, &t->tm_mday, &t->tm_hour, &t->tm_min, &t->tm_sec);
		t->tm_hour = 0;
		t->tm_min = 0;
		t->tm_sec = 0;
	}else return false;
	if(t->tm_mday == 0) return false;
	t->tm_isdst = -1;
	t->tm_year -= 1900;
	t->tm_mon--;
	return true;
}//END: mysqlAsDate



/*
 * Возвращает TIME значение поля в структуру struct tm
 */
bool
mysqlAsTime(mysql_s * instance, int index, struct tm * t){
	bool def = false;
	_mysql_data_check(index);
	memset(t, '\0', sizeof(struct tm));
	if(instance->fields[index].type == DT_TIME){
		sscanf(instance->data_row[index],"%02d:%02d:%02d", &t->tm_hour, &t->tm_min, &t->tm_sec);
	}else if(instance->fields[index].type == DT_DATETIME){
		sscanf(instance->data_row[index], "%04d-%02d-%02d %02d:%02d:%02d", &t->tm_year, &t->tm_mon, &t->tm_mday, &t->tm_hour, &t->tm_min, &t->tm_sec);
		if(t->tm_mday == 0) return false;
		t->tm_year = 0;
		t->tm_mon = 0;
		t->tm_mday = 0;
	}else return false;
	return true;
}//END: mysqlAsTime



/*
 * Возвращает DATETIME значение поля в структуру struct tm
 */
bool
mysqlAsDatetime(mysql_s * instance, int index, struct tm * t){
	bool def = false;
	_mysql_data_check(index);
	memset(t, '\0', sizeof(struct tm));
	if(instance->fields[index].type == DT_DATETIME){
		sscanf(instance->data_row[index], "%04d-%02d-%02d %02d:%02d:%02d", &t->tm_year, &t->tm_mon, &t->tm_mday, &t->tm_hour, &t->tm_min, &t->tm_sec);
		if(t->tm_mday == 0) return false;
		t->tm_isdst = -1;
		t->tm_year -= 1900;
		t->tm_mon--;
	}else return false;
	return true;
}//END: mysqlAsDatetime



/*
 * Возвращает значение поля в структуре KV
 */
static kv_s *
_mysqlAsKV(mysql_s * instance, int index, rowas_e rowas){
	char * ptr = instance->data_row[index];
	kv_s * kv = kvNew();
#ifdef KV_KEY_NAME_IS_DYNAMIC
	if(rowas == ROWAS_OBJECT) kv->key_name = hashStringCloneCaseN(instance->fields[index].name, instance->fields[index].name_len, &kv->key_len, &kv->key_hash);
#else
	if(rowas == ROWAS_OBJECT) hashStringCopyCaseN(kv->key_name, instance->fields[index].name, min(KV_KEY_NAME_LEN, instance->fields[index].name_len),&kv->key_len, &kv->key_hash);
#endif

	switch(instance->fields[index].type){
		case DT_NULL:
		break;

		case DT_BOOL:
			kv->type = KV_BOOL;
			if(ptr && *ptr!='0') kv->value.v_bool = true;
		break;

		case DT_INT32:
		case DT_INT64:
			kv->type = KV_INT;
			kv->value.v_int = (int64_t)stringToInt64(ptr, NULL);
		break;

		case DT_DOUBLE:
			kv->type = KV_DOUBLE;
			kv->value.v_double = (double)atof(ptr);
		break;

		case DT_STRING:
		case DT_BINARY:
		case DT_DATE:
		case DT_TIME:
		case DT_DATETIME:
		case DT_TIMESTAMP:
		case DT_ENUM:
		default:
			kv->type = KV_STRING;
			if(!ptr){
				kv->value.v_string.ptr = stringCloneN("", 0, &kv->value.v_string.len);
			}else{
				kv->value.v_string.ptr = stringCloneN(ptr, (uint32_t)instance->data_lengths[index], &kv->value.v_string.len);
			}
		break;
	}//switch

	return kv;
}//END: _mysqlAsKV



/*
 * Возвращает значение поля в структуре KV
 */
kv_s *
mysqlAsKV(mysql_s * instance, int index, rowas_e rowas){
	kv_s * def = NULL;
	_mysql_data_check(index);
	return _mysqlAsKV(instance, index, rowas);
}//END: mysqlAsKV



/*
 * Возвращает объект KV из текущей строки результатов выборки
 */
kv_s *
mysqlRowAsKV(mysql_s * instance, rowas_e rowas, kv_s * parent){
	if(instance->state != DB_INSTANCE_DATA_WORKING || !instance->data_row) return NULL;
	if(!parent) parent = kvNewRoot();
	kv_s * node;
	register int i;
	for(i = 0; i < instance->fields_count; i++){
		if((node = _mysqlAsKV(instance, i, rowas))!= NULL){
			kvInsert(parent, node, KV_INSERT);
		}else{
			DEBUG_MSG("mysqlRowAsKV() error: node returned NULL for field [%s] (%s) = [%s]", instance->fields[i].name, dtString(instance->fields[i].type), instance->data_row[i]);
		}
	}
	return parent;
}//END: mysqlRowAsKV



/*
 * Возвращает объект KV из текущей строки результатов выборки где каждый элемент представлен в виде текстовой строки
 */
kv_s *
mysqlRowAsStringKV(mysql_s * instance, rowas_e rowas, kv_s * parent){
	if(instance->state != DB_INSTANCE_DATA_WORKING || !instance->data_row) return NULL;
	if(!parent) parent = kvNewRoot();
	register int i;
	for(i = 0; i < instance->fields_count; i++){
		if(rowas == ROWAS_OBJECT) kvSetString(kvAppend(parent, instance->fields[i].name, instance->fields[i].name_len, KV_INSERT), instance->data_row[i], instance->data_lengths[i]);
		else kvSetString(kvAppend(parent, NULL, 0, KV_INSERT), instance->data_row[i], instance->data_lengths[i]);
	}
	return parent;
}//END: mysqlRowAsStringKV



/*
 * Возвращает значение поля в формате JSON
 */
static buffer_s *
_mysqlAsJson(mysql_s * instance, int index, buffer_s * buf){
	if(!buf) buf = bufferCreate(0);
	const char * data = instance->data_row[index];

	switch(instance->fields[index].type){
		case DT_NULL:
			bufferAddStringN(buf, "null", 4);
		break;
		case DT_BOOL:
			if(!data || *data == '0'){
				bufferAddStringN(buf, "false", 5);
			}else{
				bufferAddStringN(buf, "true", 4);
			}
		break;
		case DT_INT32:
		case DT_INT64:
		case DT_DOUBLE:
			bufferAddStringN(buf, data, instance->data_lengths[index]);
		break;
		default:
			if(!data){
				bufferAddStringN(buf, "null", 4);
			}else{
				bufferAddChar(buf, '"');
				encodeJson(data, instance->data_lengths[index], buf);
				bufferAddChar(buf, '"');
			}
	}//switch

	return buf;
}//END: _mysqlAsJson



/*
 * Возвращает текущую строку выборки в формате JSON
 * rowas = ROWAS_ARRAY -> ["data1","data2",...,"dataN"]
 * rowas = ROWAS_OBJECT -> {"field1":"data1","field2":"data2",...,"fieldN":"dataN"}
 */
buffer_s *
mysqlRowAsJson(mysql_s * instance, rowas_e rowas, buffer_s * buf){
	if(instance->state != DB_INSTANCE_DATA_WORKING || !instance->data_row) return NULL;
	if(!buf) buf = bufferCreate(0);
	register int i;
	//JSON объект
	if(rowas == ROWAS_OBJECT){
		bufferAddChar(buf, '{');
		for(i=0; i < instance->fields_count; i++){
			if(!i){
				bufferAddChar(buf, '"');
			}else{
				bufferAddStringN(buf, ",\"", 2);
			}
			encodeJson(instance->fields[i].name, instance->fields[i].name_len, buf);
			bufferAddStringN(buf, "\":", 2);
			_mysqlAsJson(instance, i, buf);
		}//for
		bufferAddChar(buf, '}');
	}
	//JSON массив
	else{
		bufferAddChar(buf, '[');
		for(i=0; i < instance->fields_count; i++){
			if(i>0) bufferAddChar(buf, ',');
			_mysqlAsJson(instance, i, buf);
		}//for
		bufferAddChar(buf, ']');
	}
	return buf;
}//END: mysqlRowAsJson



/*
 * Делает SQL запрос к базе на выборку данных и возвращает результат в JSON
 */
buffer_s *
mysqlSelectAsJson(mysql_s * instance, const char * query, rowas_e rowas, buffer_s * buf){
	if(!mysqlQuery(instance, query, 0) || !mysqlUseResult(instance)) return NULL;
	if(!buf) buf = bufferCreate(0);
	int i = 0;
	bufferAddChar(buf, '[');
	while(mysqlFetchRow(instance)){
		if(i++>0) bufferAddChar(buf, ',');
		mysqlRowAsJson(instance, rowas, buf);
	}
	bufferAddChar(buf, ']');
	return buf;
}//END: mysqlSelectAsJson



/*
 * Делает SQL запрос к базе на выборку данных и возвращает массив в формате JSON, состоящий из первого столбца каждой строки выборки
 */
buffer_s *
mysqlSelectFieldAsJson(mysql_s * instance, const char * query, buffer_s * buf){
	if(!mysqlQuery(instance, query, 0) || !mysqlUseResult(instance)) return NULL;
	if(!buf) buf = bufferCreate(0);
	int i = 0;
	bufferAddChar(buf, '[');
	while(mysqlFetchRow(instance)){
		if(i++>0) bufferAddChar(buf, ',');
		_mysqlAsJson(instance, 0, buf);
	}
	bufferAddChar(buf, ']');
	return buf;
}//END: mysqlSelectFieldAsJson



/*
 * Выборка данных в объект KV с ключем, получаемым из значений поля field
 * query - SQL запрос или NULL если запрос генерируется через mysqlTemplate
 * index - индекс поля, используемого в качестве ключа
 * rowas - формат строки данных: как объект или как массив
 * value_as_array - если TRUE, то значением ключа будет массив, содержащий строки выборки с указанным ключем (по сути группировка строк по ключу)
 * 					если FALSE, то каждому ключу будет соответствовать только одна строка выборки (первая), остальные будут игнорироваться
 */
kv_s *
mysqlSelectByKey(mysql_s * instance, const char * query, int index, rowas_e rowas, bool value_as_array, kv_s * parent){
	if(!mysqlQuery(instance, query, 0) || !mysqlUseResult(instance)) return NULL;
	if(index < 0 || index >= instance->fields_count) return NULL;
	if(!parent) parent = kvNewRoot();
	register const char * key;
	register uint32_t key_len;
	register kv_s * key_node;
	register kv_s * value_node;

	while(mysqlFetchRow(instance)){
		key = (const char *) instance->data_row[index];
		key_len = (uint32_t) instance->data_lengths[index];
		key_node = kvAppend(parent, key, key_len, KV_REPLACE);
		if(value_as_array){
			value_node = kvAppend(key_node, NULL, 0, KV_INSERT);
		}else{
			if(key_node->type != KV_NULL) continue;
			value_node = key_node;
		}
		mysqlRowAsKV(instance, rowas, value_node);
	}//mysqlFetchRow

	return parent;
}//END: mysqlSelectByKey



/*
 * Выборка данных в строку формата JSON с ключем, получаемым из значений поля field
 * query - SQL запрос или NULL если запрос генерируется через mysqlTemplate
 * index - индекс поля, используемого в качестве ключа
 * rowas - формат строки данных: как объект или как массив
 * value_as_array - если TRUE, то значением ключа будет массив, содержащий строки выборки с указанным ключем (по сути группировка строк по ключу)
 * 					если FALSE, то каждому ключу будет соответствовать только одна строка выборки (первая), остальные будут игнорироваться
 */
buffer_s *
mysqlSelectByKeyAsJson(mysql_s * instance, const char * query, int index, rowas_e rowas, bool value_as_array, buffer_s * buffer){
	if(!mysqlQuery(instance, query, 0) || !mysqlUseResult(instance)) return NULL;
	if(index < 0 || index >= instance->fields_count) return NULL;
	register kv_s * root = kvNewRoot();
	register const char * key;
	register uint32_t key_len;
	register kv_s * key_node;
	register kv_s * value_node;
	buffer_s * buf;

	while(mysqlFetchRow(instance)){
		key = (const char *) instance->data_row[index];
		key_len = (uint32_t) instance->data_lengths[index];
		key_node = kvAppend(root, key, key_len, KV_REPLACE);
		if(value_as_array){
			value_node = kvAppend(key_node, NULL, 0, KV_INSERT);
		}else{
			if(key_node->type != KV_NULL) continue;
			value_node = key_node;
		}
		buf = mysqlRowAsJson(instance, rowas, NULL);
		kvSetJsonPtr(value_node, buf->buffer, buf->count);
		mFree(buf);
	}//mysqlFetchRow

	buf = kvEcho(root, KVF_JSON, buffer);
	kvFree(root);
	return buf;
}//END: mysqlSelectByKeyAsJson



/*
 * Возвращает первую строку выборки в объекте KV, остальные строки игнорируются (SQL запросы рекомендуется строить с LIMIT 1)
 */
kv_s * 
mysqlSelectRecord(mysql_s * instance, const char * query, size_t query_len, rowas_e rowas, kv_s * parent){
	if(!mysqlQuery(instance, query, query_len) || !mysqlUseResult(instance)) return NULL;
	kv_s * result = mysqlRowAsKV(instance, rowas, parent);
	while(mysqlFetchRow(instance));
	return result;
}//END: mysqlSelectRecord



/*
 * Возвращает первую строку выборки в формате JSON, остальные строки игнорируются (SQL запросы рекомендуется строить с LIMIT 1)
 */
buffer_s * 
mysqlSelectRecordAsJson(mysql_s * instance, const char * query, size_t query_len, rowas_e rowas, buffer_s * buffer){
	if(!mysqlQuery(instance, query, query_len) || !mysqlUseResult(instance)) return NULL;
	buffer_s * result = mysqlRowAsJson(instance, rowas, buffer);
	while(mysqlFetchRow(instance));
	return result;
}//END: mysqlSelectRecordAsJson



/*
 * Возвращает первый столбец первой строки выборки как STRING значение, все остальное игнорируется (SQL запросы рекомендуется строить с LIMIT 1)
 */
char *
mysqlResultAsString(mysql_s * instance, const char * query, size_t query_len, uint32_t * olen){
	if(!mysqlQuery(instance, query, query_len) || !mysqlUseResult(instance)) return NULL;
	char * result = NULL;
	bool ok = false;
	if(mysqlFetchRow(instance)){
		ok = mysqlAsString(instance, 0, &result, olen);
		while(mysqlFetchRow(instance));
		if(ok) return result;
	}
	return stringCloneN("", 1, olen);
}//END: mysqlResultAsString



/*
 * Возвращает первый столбец первой строки выборки как JSON значение, все остальное игнорируется (SQL запросы рекомендуется строить с LIMIT 1)
 */
buffer_s *
mysqlResultAsJson(mysql_s * instance, const char * query, size_t query_len, buffer_s * buf){
	if(!mysqlQuery(instance, query, query_len) || !mysqlUseResult(instance)) return NULL;
	if(mysqlFetchRow(instance)){
		if(!buf) buf =  bufferCreate(0);
		mysqlAsJson(instance, 0, buf);
		while(mysqlFetchRow(instance));
		return buf;
	}
	return NULL;
}//END: mysqlResultAsJson



/*
 * Возвращает первый столбец первой строки выборки как Bool значение, все остальное игнорируется (SQL запросы рекомендуется строить с LIMIT 1)
 */
bool
mysqlResultAsBool(mysql_s * instance, const char * query, size_t query_len, bool * value){
	if(!mysqlQuery(instance, query, query_len) || !mysqlUseResult(instance)) return false;
	if(mysqlFetchRow(instance)){
		char * ptr = instance->data_row[0];
		if(value) *value = (ptr && *ptr != '0' ? true : false);
		while(mysqlFetchRow(instance));
		return true;
	}
	return false;
}//END: mysqlResultAsBool



/*
 * Возвращает первый столбец первой строки выборки как Double значение, все остальное игнорируется (SQL запросы рекомендуется строить с LIMIT 1)
 */
bool
mysqlResultAsDouble(mysql_s * instance, const char * query, size_t query_len, double * value){
	if(!mysqlQuery(instance, query, query_len) || !mysqlUseResult(instance)) return false;
	if(mysqlFetchRow(instance)){
		if(value) *value = (double)atof( instance->data_row[0] );
		while(mysqlFetchRow(instance));
		return true;
	}
	return false;
}//END: mysqlResultAsDouble



/*
 * Возвращает первый столбец первой строки выборки как Int32 значение, все остальное игнорируется (SQL запросы рекомендуется строить с LIMIT 1)
 */
bool
mysqlResultAsInt32(mysql_s * instance, const char * query, size_t query_len, int32_t * value){
	if(!mysqlQuery(instance, query, query_len) || !mysqlUseResult(instance)) return false;
	if(mysqlFetchRow(instance)){
		if(value) *value = (int32_t)atoi( instance->data_row[0] );
		while(mysqlFetchRow(instance));
		return true;
	}
	return false;
}//END: mysqlResultAsInt32




/*
 * Возвращает первый столбец первой строки выборки как Int64 значение, все остальное игнорируется (SQL запросы рекомендуется строить с LIMIT 1)
 */
bool
mysqlResultAsInt64(mysql_s * instance, const char * query, size_t query_len, int64_t * value){
	if(!mysqlQuery(instance, query, query_len) || !mysqlUseResult(instance)) return false;
	if(mysqlFetchRow(instance)){
		if(value) *value = (int64_t)stringToInt64(instance->data_row[0], NULL);
		while(mysqlFetchRow(instance));
		return true;
	}
	return false;
}//END: mysqlResultAsInt64






/***********************************************************************
 * SELECT
 **********************************************************************/ 

/*
 * Запрос SELECT
 */
inline bool
mysqlSelect(mysql_s * instance, const char * query, size_t query_len){
	if(!mysqlQuery(instance, query, query_len) || !mysqlUseResult(instance)) return false;
	return true;
}//END: mysqlSelect







/***********************************************************************
 * INSERT
 **********************************************************************/

/*
 * Вставка записи в таблицу базы данных
 */
inline bool
mysqlInsert(mysql_s * instance, const char * table, kv_s * fields, kv_s * defaults){
	if(!fields || fields->type != KV_OBJECT) return false;
	kv_s * kv = NULL;
	kv_s * node, * tmp;
	//Если полей со значениями по-умолчанию не задано, проводим вставку с теми полями, которые переданы в fields
	if(!defaults || defaults->type != KV_OBJECT){
		kv = kvCopy(NULL, fields);
	}
	//Если значения по-умолчанию заданы, фильтруем fields и выбираем только нужные поля
	else{
		kv = kvCopy(NULL, defaults);
		for(node = fields->value.v_list.first; node != NULL; node = node->next){
			#ifdef KV_KEY_NAME_IS_DYNAMIC
			if(!node->key_name || !node->key_len) continue;
			#else
			if(node->key_name[0]=='\0' || !node->key_len) continue;
			#endif
			if(node->type == KV_POINTER || node->type == KV_FUNCTION || node->type == KV_OBJECT || node->type == KV_ARRAY) continue;
			if((tmp = kvSearchHash(kv, node->key_name, node->key_len, node->key_hash)) != NULL){
				kvCopy(tmp, node);
			}
		}//for
	}

	//Создание запроса: INSERT INTO `table` (`field1`,`field2`,...,`fieldN`) VALUES (val1,val2,...,valN)
	buffer_s * buf = bufferCreate(0);
	uint32_t   i, count = 0;
	bufferAddStringN(buf, CONST_STR_COMMA_LEN("INSERT INTO `"));
	bufferAddString(buf, table);
	bufferAddStringN(buf, CONST_STR_COMMA_LEN("` ("));
	for(node = kv->value.v_list.first; node != NULL; node = node->next){
		if(count>0) bufferAddChar(buf, ',');
		bufferAddChar(buf, '`');
		bufferAddStringN(buf, node->key_name, node->key_len);
		bufferAddChar(buf, '`');
		count++;
	}
	bufferAddStringN(buf, CONST_STR_COMMA_LEN(")VALUES("));
	for(i=0; i<count; i++){
		if(i>0) bufferAddChar(buf, ',');
		bufferAddChar(buf, '?');
	}
	bufferAddChar(buf, ')');

	mysqlTemplate(instance, buf->buffer, buf->count, true);
	mysqlBindKV(instance, kv);

	bool result = mysqlQuery(instance, NULL, 0);

	bufferFree(buf);
	kvFree(kv);
	return result;
}//END: mysqlInsert




/*
 * Обновление записи в таблице базы данных
 * instance - экземпляр базы данных
 * table - таблица в которой выполнятеся обновление
 * conditions - условия для WHERE
 * fields - обновляемые поля
 * defaults - поля по-умолчанию для данной таблицы
 */
inline bool
mysqlUpdate(mysql_s * instance, const char * table, kv_s * conditions, kv_s * fields, kv_s * defaults){
	if(!fields || fields->type != KV_OBJECT) return false;
	if(!conditions || conditions->type != KV_OBJECT) conditions = NULL;
	kv_s * update_kv = NULL;
	kv_s * where_kv = NULL;
	kv_s * node;
	//Если полей со значениями по-умолчанию не задано, проводим вставку с теми полями, которые переданы в fields
	if(!defaults || defaults->type != KV_OBJECT){
		update_kv = kvCopy(NULL, fields);
		if(conditions) where_kv = kvCopy(NULL, conditions);
	}
	//Если значения по-умолчанию заданы, фильтруем fields и выбираем только нужные поля
	else{
		update_kv = kvIntersect(fields, defaults);
		if(conditions) where_kv = kvIntersect(conditions, defaults);
	}

	//Ничего не произошло
	if(!update_kv->value.v_list.first){
		kvFree(update_kv);
		kvFree(where_kv);
		return true;
	}

	//Создание запроса: UPDATE `table` SET `field1`=?,`field2`=?,...,`fieldN`=? WHERE `cond1`=? AND `cond2`=?
	buffer_s * buf = bufferCreate(0);

	bufferAddStringN(buf, CONST_STR_COMMA_LEN("UPDATE `"));
	bufferAddString(buf, table);
	bufferAddStringN(buf, CONST_STR_COMMA_LEN("` SET "));

	for(node = update_kv->value.v_list.first; node != NULL; node = node->next){
		if(node != update_kv->value.v_list.first) bufferAddChar(buf, ',');
		bufferAddChar(buf, '`');
		bufferAddStringN(buf, node->key_name, node->key_len);
		bufferAddStringN(buf, CONST_STR_COMMA_LEN("`=?"));
	}

	if(where_kv && where_kv->value.v_list.first){
		bufferAddStringN(buf, CONST_STR_COMMA_LEN(" WHERE "));
		for(node = where_kv->value.v_list.first; node != NULL; node = node->next){
			if(node != where_kv->value.v_list.first) bufferAddStringN(buf, CONST_STR_COMMA_LEN(" AND "));
			bufferAddChar(buf, '`');
			bufferAddStringN(buf, node->key_name, node->key_len);
			bufferAddStringN(buf, CONST_STR_COMMA_LEN("`=?"));
		}
	}

	mysqlTemplate(instance, buf->buffer, buf->count, true);
	mysqlBindKV(instance, update_kv);
	if(where_kv && where_kv->value.v_list.first) mysqlBindKV(instance, where_kv);

	bool result = mysqlQuery(instance, NULL, 0);

	bufferFree(buf);
	kvFree(update_kv);
	kvFree(where_kv);
	return result;
}//END: mysqlUpdate











/***********************************************************************
 * Формирование условий запроса из KV в SQL
 **********************************************************************/ 


/*
 * Построение части SQL запроса на основании данных массива условий
 * 
 * conditions - массив или объект условий
 * separator - связка между условиями: 
 * 			если часть SQL запроса будет как перечисление полей для UPDATE, используйте ","
 * 			если часть SQL запроса будет после WHERE или ON, используйте для связки "AND" или "OR" в зависимости от запроса
 * 			если separator задан как NULL, то используется связка " AND "
 * prefix - Подстановка перед каждым полем названия таблицы: prefix="mytable" -> mytable.`field`
 * 			Префикс не обрамляется обратными кавычками, если нужно обрамление таблицы, передавайте ее имя в массиве условий вместе с полем: 
 * buf - Буфер для вывода SQL, если буфер не задан (NULL), то будет создан новый буфер, если буфер задан, то запись вывода будет производиться в него
 * 
 * Запись в conditions:
 * conditions = array(
 * 
 * 		'testfield=25',			#Так задается SQL текст, который не будет вставлен в результирующую SQL строку без каких-либо изменений
 * 
 * 		'myfield' => 'test',	#Так задается конструкция [поле][=][значение], поле и значение квотируются, 
 * 								#между ними применяется оператор равенства, результатом для MySQL будет: `myfield`='test'
 * 
 * 		'field2'=>array(1,2,3),	#Так задается конструкция [поле] IN ([значение1],[значение2],[значение3]), поле и значение квотируются,
 * 								#между ними применяется оператор IN (входит в перечисление), результатом для MySQL будет: `field2` IN ('1','2','3')
 * 
 * 		array(							#Так задается произвольная конструкция вида [поле][=][значение], если значение value является массивом, 
 * 			'field' => 'test',			#То обработка массива осуществялется в зависимости от значения в bridge (если не задано, по умолчанию ",")
 * 			'value' => array(1,2,3),	#при bridge="," -> `test` NOT IN (1,2,3)
 * 			'glue' => 'NOT IN',			#при bridge="OR"("AND") -> (`test` NOT IN (1) OR `test` NOT IN (2) OR `test` NOT IN (3))
 * 			'bridge' => ',',			#за исключением, когда оператор задан как "BETWEEN", bridge в этом случае не используется
 * 			'type' => BIND_NUM			#будет обработано только 2 элемента массива value -> `test` BETWEEN 1 AND 2
 * 		),
 * 		array('test',array(1,2,3),'NOT IN',',',BIND_NUM)	#Альтернативная запись вышеуказанного массива в неассоциированном виде, где элементы:
 * 															#[0]-поле(*), [1]-значение(null), [2]-тип данных(BIND_TEXT), [3]-оператор(= или IN), [4]-связка(,)
 * );
 * 
 * Примеры элементов в массиве $conditions и результат (для MySQL):
 * "test != 'xxx'"													-> test != 'xxx'
 * 'test' => 'xxx'													-> `test`='xxx'
 * 'test' => null													-> `test`=NULL
 * 'test' => array(1,2,3)											-> `test` IN ('1','2','3')
 * array('test','xxx') 												-> `test`='xxx'
 * array('test',123,null,'>=') 										-> `test`>='123'
 * array('test',999,BIND_NUM,'!=','') 								-> `test`!=999
 * array('test',array(1,2,3)) 										-> `test` IN ('1','2','3')
 * array('field'=>'test','value'=>array(1,2,3),'type'=>BIND_NUM) 	-> `test` IN (1,2,3)
 * array('test',array(1,2,3),BIND_NUM,'NOT IN','') 					-> `test` NOT IN (1,2,3)
 * array('test',array(1,2,3),BIND_NUM,'!=','AND')					-> (`test` != 1 AND `test` != 2 AND `test` != 3)
 * array('test',array(4,8,6),'','BETWEEN') 							-> `test` BETWEEN '4' AND '8' <<< '6' отсекается, используется только первые два элемента массива
 * array('test',array(4,8),BIND_NUM,'BETWEEN','') 					-> `test` BETWEEN 4 AND 8
 * array('test') 													-> `test` = NULL
 * array('test',null) 												-> `test` = NULL
 * array('test',null,'','!=') 										-> `test` !=NULL
 * array('','SELECT field FROM table WHERE field>4',BIND_SQL)		-> (SELECT field FROM table WHERE field>4)
 * 
 * Примеры некорректных элементов:
 * array('test',9,BIND_NUM,'BETWEEN','') 							-> `test` BETWEEN 9 <<< некорректный SQL!
 * array('test',array()) 											-> `test`='' <<< внимание!
 * array('test',array(),'','IN') 									-> `test` IN '' <<< внимание! некорректный SQL!
 * array('test',9,'','IN') 											-> `test` IN '9' <<< некорректный SQL!
 * array()															->  <<< некорректно, будет пропущено
 * null,															->  <<< некорректно, будет пропущено
 * 
 * Пример вызова:
 * $sql_conditions = $db->buildSqlConditions(array(
 * array('test',array(1,2,3),'!=','AND',BIND_NUM)
 * ),'AND');
 */
/*
buffer_s *
mysqlConditions(kv_s * conditions, const char * separator, const char * prefix, buffer_s * buf){
	if(!conditions || (conditions->type != KV_OBJECT && conditions->type != KV_ARRAY)) return NULL;
	if(!separator) separator = " AND ";
	kv_s * condition;
	if(!buf) buf = bufferCreate(0);
	bool value_is_array;
	bool key_is_field;
	uint32_t cond_n = 0;
	const char * cond;

	#define _addfield() do{ \
		if(cond_n > 0) bufferAddString(buf, separator); \
		bufferAddChar(buf, ' '); \
		if(prefix){ \
			bufferAddString(buf, prefix); \
			bufferAddStringN(buf, CONST_STR_COMMA_LEN(".`")); \
		}else{ \
			bufferAddChar(buf, '`'); \
		} \
		bufferAddStringN(buf, condition->key_name, condition->key_len); \
		bufferAddChar(buf, '`'); \
	while(0)

	#define _addcond(cond) bufferAddString(buf, cond);

	//Последовательная обработка условий
	for(condition = conditions->value.v_list.first; condition != NULL; condition = condition->next){
		if(condition->type == KV_POINTER || condition->type == KV_FUNCTION) continue;
		value_is_array	= (condition->type == KV_ARRAY || condition->type == KV_OBJECT);
		key_is_field	= (condition->key_name != NULL && *condition->key_name > 0 && condition->key_len > 0);

		//Если имя ключа (поля) не задано, а значение задано как текст, то считает что это SQL вставка
		if(!key_is_field && condition->type == KV_STRING){
			bufferAddStringN(buf, condition->value.v_string.ptr, condition->value.v_string.len);
			continue;
		}

		//Если значение не массив и не объект, и задано имя поля (ключ)
		//то считаем, что это пара ключ => значение
		if(key_is_field){


// autoselect
#define COND_AUTO 0
// =
#define COND_EQUAL 1
// <>
#define COND_INEQUAL 2
// >
#define COND_MORE 3
// <
#define COND_LESS 4
// IN
#define COND_IN 5
// NOT IN
#define COND_NOTIN 6
// BETWEEN
#define COND_IN 7
// LIKE
#define COND_LIKE 8
// LIKE%
#define COND_LIKEX 9
// %LIKE
#define COND_XLIKE 10
// %LIKE%
#define COND_XLIKEX 11
// NOT LIKE
#define COND_NOTLIKE 12
// NOT LIKE%
#define COND_NOTLIKEX 13
// NOT %LIKE
#define COND_NOTXLIKE 14
// NOT %LIKE%
#define COND_NOTXLIKEX 15

			bufferAddStringN(buf, CONST_STR_COMMA_LEN("`="));
			switch(condition->type){

				case KV_NULL: 
					switch(condition->flags){
						case COND_INEQUAL: cond = "<>"; break;
						case COND_AUTO:
						case COND_EQUAL: 
						default:  cond = "="; break;
					}
					bufferAddString(buf, cond); 
				break;

				case KV_BOOL: 
					bufferAddChar(buf, (condition->value.v_bool ? '1' : '0')); 
				break;

				case KV_INT: bufferAddInt(buf, condition->value.v_int);  break;
				case KV_DOUBLE: bufferAddDouble(buf, condition->value.v_double);  break;
				case KV_JSON:
				case KV_STRING: mysqlEscapeBuffer(condition->value.v_string.ptr, condition->value.v_string.len, true, buf); break;
				case KV_DATETIME: bufferAddDatetime(buf, condition->value.v_datetime.ts, (condition->value.v_datetime.format ? condition->value.v_datetime.format : MYSQL_DATETIME_FORMAT)); break;
				default: break;
			}//switch condition->type
			cond_n++;
			continue;
		}

		
		
		
		
	}//for conditions

	#undef _addfield
	#undef _addcond

	return buf;
}//END: mysqlConditions

*/















