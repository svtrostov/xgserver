/***********************************************************************
 * XG SERVER
 * core/db.c
 * Работа с базой данных
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include "core.h"
#include "kv.h"
#include "db.h"
#include "globals.h"


//Мьютекс синхронизации в момент обращения к IDLE списку
static pthread_mutex_t db_idle_mutex = PTHREAD_MUTEX_INITIALIZER;

static sqltemplate_s * _sqltemplate_idle_list = NULL;
static bind_s * _bind_idle_list = NULL;


#define _toIdle(f_name, d_type, d_list, d_mutex) static void f_name(d_type * item){	\
	if(!item) item = (d_type *)mNewZ(sizeof(d_type));	\
	pthread_mutex_lock(&d_mutex);	\
		item->next = d_list;	\
		d_list = item;	\
	pthread_mutex_unlock(&d_mutex);	\
}


#define _fromIdle(f_name, d_type, d_list, d_mutex) static d_type * f_name(void){	\
	d_type * item = NULL;	\
	pthread_mutex_lock(&d_mutex);	\
		if(d_list){	\
			item = d_list;	\
			d_list = item->next;	\
		}	\
	pthread_mutex_unlock(&d_mutex);	\
	if(!item){	\
		item = (d_type *)mNewZ(sizeof(d_type));	\
	}else{	\
		memset(item,'\0',sizeof(d_type));	\
	}	\
	return item;	\
}

//Добавляет новый/существующмй элемент в IDLE список
_toIdle(_templateToIdle, sqltemplate_s, _sqltemplate_idle_list, db_idle_mutex);

//Получает элемент из IDLE списка или создает новый, если IDLE список пуст
_fromIdle(_templateFromIdle, sqltemplate_s, _sqltemplate_idle_list, db_idle_mutex);

//Добавляет новый/существующмй элемент в IDLE список
_toIdle(_bindToIdle, bind_s, _bind_idle_list, db_idle_mutex);

//Получает элемент из IDLE списка или создает новый, если IDLE список пуст
_fromIdle(_bindFromIdle, bind_s, _bind_idle_list, db_idle_mutex);




/***********************************************************************
 * Инициализация
 **********************************************************************/ 

/*
 * Инициализация механизмов работы с базами данных
 */
void
dbInit(void){

	if(mysql_library_init(0, NULL, NULL)) FATAL_ERROR("could not initialize MySQL library\n");

	kv_s * instances = kvGetByPath(XG_CONFIG, "database");
	if(!instances || instances->type != KV_OBJECT) return;
	const char * driver;
	kv_s * instance;

	uint32_t instances_count = 0;

	//Просмотр всех настроек соединения
	for(instance = instances->value.v_list.first; instance != NULL; instance = instance->next){
		if(instance->type != KV_OBJECT || !instance->key_name || !instance->key_len) continue;
		if(!kvGetBoolByPath(instance,"active",false)) continue;	//Если соединение отключено
		if((driver = kvGetStringByPath(instance, "driver", NULL))==NULL) continue;

		instances_count++;

		//MySQL
		if(stringCompareCase(driver, "mysql")){
			mysqlAddInstanceOptions(instance->key_name, dbGetMysqlConfig(instance));
		}
		/*
		else
		//MS SQL
		if(stringCompareCase(driver, "mssql")){
			
			
		}else
		//PG SQL
		if(stringCompareCase(driver, "pgsql")){
			
			
		}else
		//Oracle
		if(stringCompareCase(driver, "oracle")){
			
			
		}
		*/

	}//Просмотр всех настроек соединения


	int i;
	instances_count *= 32;
	for(i=0; i<instances_count; i++) _templateToIdle(NULL);
	instances_count *= 8;
	for(i=0; i<instances_count; i++) _bindToIdle(NULL);

}//END: dbInit



/*
 * Завершение работы с базами данных
 */
void
dbEnd(void){
	mysql_library_end();
}//END: dbEnd



/*
 * Получение настроек MySQL из конфигурации
 */
mysql_options_s *
dbGetMysqlConfig(kv_s * instance){
	mysql_options_s * config = (mysql_options_s *)mNewZ(sizeof(mysql_options_s));
	config->username	= kvGetRequireString(instance, "username");
	config->password	= kvGetRequireString(instance, "password");
	config->database	= kvGetRequireString(instance, "database");
	config->port		= (unsigned int) kvGetIntByPath(instance, "port", 3306);
	if(config->port < 80 || config->port > 65000) FATAL_ERROR("[/database/%s/port] = [%u], out of range, value should be between 80 and 65000\n", instance->key_name, config->port);
	config->host		= kvGetRequireString(instance, "host");
	if(*config->host == '/'){
		config->unix_socket	= config->host;
		config->host		= NULL;
	}
	config->use_ssl		= kvGetBoolByPath(instance, "use_ssl", false);
	if(config->use_ssl){
		config->ssl_cert	= kvGetRequireString(instance, "certificate_file");
		if(!fileExists(config->ssl_cert)) FATAL_ERROR("[/database/%s/certificate_file]: file [%s] not found\n", instance->key_name, config->ssl_cert);
		config->ssl_key		= kvGetRequireString(instance, "private_key_file");
		if(!fileExists(config->ssl_key)) FATAL_ERROR("[/database/%s/private_key_file]: file [%s] not found\n", instance->key_name, config->ssl_key);
		config->ssl_ca		= kvGetRequireString(instance, "ca_file");
		if(!fileExists(config->ssl_key)) FATAL_ERROR("[/database/%s/ca_file]: file [%s] not found\n", instance->key_name, config->ssl_ca);
		config->ssl_cipher	= NULL;
		config->ssl_ca_path		= fileRealpath(kvGetRequireString(instance, "ca_path"), NULL);
		if(!config->ssl_ca_path) FATAL_ERROR("[/database/%s/ca_path] path not found\n", instance->key_name);
		if(!dirExists(config->ssl_ca_path)) FATAL_ERROR("[/database/%s/ca_path]: directory [%s] not found\n", instance->key_name, config->ssl_ca_path);
	}
	config->config_file		= kvGetStringByPath(instance, "config_file", NULL);
	if(config->config_file){
		if(!fileExists(config->config_file)) FATAL_ERROR("[/database/%s/config_file]: file [%s] not found\n", instance->key_name, config->config_file);
	}
	config->config_group	= kvGetStringByPath(instance, "config_group", NULL);

	return config;
}//END: dbGetMysqlConfig




/***********************************************************************
 * Типы запросов
 **********************************************************************/ 



/*
 * Анализирует SQL запрос и возвращает тип запроса
 */
sql_query_type_e
sqlQueryTypeDetect(const char * sql){

	while(isspace((int)*sql)) sql++;
	if(!*sql) return SQL_QUERY_EMPTY;

	switch(tolower((int)*sql++)){

		//alter
		case 'a':
			if(stringCompareCaseN(sql, "lter", 4)) return SQL_QUERY_ALTER;	//a lter
		break;

		//begin
		case 'b':
			if(stringCompareCaseN(sql, "egin", 4)) return SQL_QUERY_TRANSACTION;	//b egin
		break;

		//create, copy, commit
		case 'c':
			switch(tolower((int)*sql++)){
				case 'o': 
					if(stringCompareCaseN(sql, "mmit", 4)) return SQL_QUERY_COMMIT;	//co mmit
					if(stringCompareCaseN(sql, "py", 2)) return SQL_QUERY_COPY;	//co py
				break;
				case 'r': 
					if(stringCompareCaseN(sql, "eate", 4)) return SQL_QUERY_CREATE;	// cr eate
				break;
			}
		break;

		//delete, drop
		case 'd':
			switch(tolower((int)*sql++)){
				case 'e': if(stringCompareCaseN(sql, "lete", 4)) return SQL_QUERY_DELETE;	//de lete
				case 'r': if(stringCompareCaseN(sql, "op", 2)) return SQL_QUERY_DROP;	//dr op
			}
		break;

		//grant
		case 'g':
			if(stringCompareCaseN(sql, "rant", 4)) return SQL_QUERY_GRANT;	//g rant
		break;

		//insert
		case 'i':
			if(stringCompareCaseN(sql, "nsert", 5)) return SQL_QUERY_INSERT;	//i nsert
		break;

		//load data, lock
		case 'l':
			if(stringCompareCaseN(sql, "oad data", 8)) return SQL_QUERY_LOAD;	//l oad data
			if(stringCompareCaseN(sql, "ock", 3)) return SQL_QUERY_LOCK;	//l ock
		break;

		//replace, revoke, rollback
		case 'r':
			switch(tolower((int)*sql++)){
				case 'e':
					if(stringCompareCaseN(sql, "place", 5)) return SQL_QUERY_REPLACE;	//re place
					if(stringCompareCaseN(sql, "voke", 4)) return SQL_QUERY_REVOKE;	//re voke
				break;
				case 'o':
					if(stringCompareCaseN(sql, "llback", 6)) return SQL_QUERY_REPLACE;	//ro llback
				break;
			}
		break;

		//set, select, show, start transaction
		case 's':
			switch(tolower((int)*sql++)){
				case 'e':
					if(tolower((int)*sql) == 't') return SQL_QUERY_SET;	//se t
					if(stringCompareCaseN(sql, "lect", 4)) return SQL_QUERY_SELECT;	//se lect
				break;
				case 'h':
					if(stringCompareCaseN(sql, "ow", 2)) return SQL_QUERY_SHOW;	//sh ow
				break;
				case 't':
					if(stringCompareCaseN(sql, "art", 3)) return SQL_QUERY_TRANSACTION;	//st art
				break;
			}
		break;

		//truncate
		case 't':
			if(stringCompareCaseN(sql, "runcate", 7)) return SQL_QUERY_TRUNCATE;	//t runcate
		break;

		//unlock, update
		case 'u':
			switch(tolower((int)*sql++)){
				case 'n': if(stringCompareCaseN(sql, "lock", 4)) return SQL_QUERY_UNLOCK;	//un lock
				case 'p': if(stringCompareCaseN(sql, "date", 4)) return SQL_QUERY_UPDATE;	//up date
			}
		break;
	}

	return SQL_QUERY_UNDEFINED;
}//END: sqlQueryType



/*
 * Функция определяет, является ли SQL запрос изменяющим данные или нет
 */
inline bool
sqlIsChange(sql_query_type_e type){
	switch(type){
		case SQL_QUERY_UNDEFINED:
		case SQL_QUERY_EMPTY:
		case SQL_QUERY_SELECT:
		case SQL_QUERY_TRANSACTION:
		case SQL_QUERY_COMMIT:
		case SQL_QUERY_ROLLBACK:
		case SQL_QUERY_SHOW:
			return false;
		default:
			return true;
	}
}//END: sqlIsChange



/*
 * Возвращает текстовое представление типа запроса
 */
const char *
sqlQueryTypeString(sql_query_type_e type){
	switch(type){
	case SQL_QUERY_EMPTY:		return "SQL_QUERY_EMPTY";
	case SQL_QUERY_ALTER:		return "SQL_QUERY_ALTER";
	case SQL_QUERY_COPY:		return "SQL_QUERY_COPY";
	case SQL_QUERY_CREATE:		return "SQL_QUERY_CREATE";
	case SQL_QUERY_COMMIT:		return "SQL_QUERY_COMMIT";
	case SQL_QUERY_DELETE:		return "SQL_QUERY_DELETE";
	case SQL_QUERY_DROP:		return "SQL_QUERY_DROP";
	case SQL_QUERY_GRANT:		return "SQL_QUERY_GRANT";
	case SQL_QUERY_INSERT:		return "SQL_QUERY_INSERT";
	case SQL_QUERY_LOAD:		return "SQL_QUERY_LOAD";
	case SQL_QUERY_LOCK:		return "SQL_QUERY_LOCK";
	case SQL_QUERY_REPLACE:		return "SQL_QUERY_REPLACE";
	case SQL_QUERY_REVOKE:		return "SQL_QUERY_REVOKE";
	case SQL_QUERY_ROLLBACK:	return "SQL_QUERY_ROLLBACK";
	case SQL_QUERY_SELECT:		return "SQL_QUERY_SELECT";
	case SQL_QUERY_SET:			return "SQL_QUERY_SET";
	case SQL_QUERY_SHOW:		return "SQL_QUERY_SHOW";
	case SQL_QUERY_TRUNCATE:	return "SQL_QUERY_TRUNCATE";
	case SQL_QUERY_TRANSACTION:	return "SQL_QUERY_TRANSACTION";
	case SQL_QUERY_UNLOCK:		return "SQL_QUERY_UNLOCK";
	case SQL_QUERY_UPDATE:		return "SQL_QUERY_UPDATE";
	case SQL_QUERY_UNDEFINED:
	default:					return "SQL_QUERY_UNDEFINED";
	}
}//END: sqlQueryTypeString



/*
 * Возвращает текстовое представление типа dt_t
 */
const char *
dtString(dt_t type){
	switch(type){
		case DT_NULL:		return "DT_NULL";
		case DT_BOOL:		return "DT_BOOL";
		case DT_INT32:		return "DT_INT32";
		case DT_INT64:		return "DT_INT64";
		case DT_DOUBLE:		return "DT_DOUBLE";
		case DT_STRING:		return "DT_STRING";
		case DT_BINARY:		return "DT_BINARY";
		case DT_DATE:		return "DT_DATE";
		case DT_TIME:		return "DT_TIME";
		case DT_DATETIME:	return "DT_DATETIME";
		case DT_TIMESTAMP:	return "DT_TIMESTAMP";
		case DT_ENUM:		return "DT_ENUM";
		default:
			return "UNDEFINED";
	}
}//END: dtString





/***********************************************************************
 * Параметризованный запрос
 **********************************************************************/ 

/*
 * Задает SQL шаблон
 */
sqltemplate_s *
sqlTemplate(const char * sql_template, size_t template_len, bool is_const, db_driver_e driver){
	sqltemplate_s * template = _templateFromIdle();
	template->driver	= driver;
	template->is_const	= is_const;
	if(!sql_template){
		template->is_const	= true;
		template->query_const = "";
		template->query_len = 0;
	}else{
		if(is_const){
			template->query_const	= sql_template;
			template->query_len		= (template_len > 0 ? template_len : strlen(sql_template));
		}else{
			template->query_char	= (template_len > 0 ? stringCloneN(sql_template, template_len, &template->query_len) : stringClone(sql_template, &template->query_len));
		}
	}
	return template;
}//END: sqlTemplateNew



/*
 * Удаление SQL шаблона
 */
void
sqlTemplateFree(sqltemplate_s * template){
	if(!template->is_const && template->query_char) mFree(template->query_char);
	if(template->first) bindQueueFree(template->first);
	_templateToIdle(template);
}//END: sqlTemplateFree



/*
 * Удаление цепочки вставок
 */
void
bindQueueFree(bind_s * bind){
	bind_s * current;
	while(bind){
		current = bind;
		bind = current->next;
		if(!current->is_const && current->v_char) mFree(current->v_char);
		_bindToIdle(current);
	}
}//END: bindQueueFree



/*
 * Добавляет новую вставку в параметризованный запрос
 */
bind_s *
bindNew(sqltemplate_s * template){
	bind_s * bind = _bindFromIdle();
	if(template->last) template->last->next = bind;
	if(!template->first) template->first = bind;
	template->last = bind;
	return bind;
}//END: bindNew



/*
 * Добавляет вставку типа NULL
 */
bind_s *
bindNull(sqltemplate_s * template){
	bind_s * bind = bindNew(template);
	bind->is_const = true;
	switch(template->driver){
		case DB_DRIVER_MYSQL:
		default:
			bind->v_const = "NULL";
			bind->v_len = 4;
	}
	template->binds_len += bind->v_len;
	return bind;
}//END: bindNull



/*
 * Добавляет вставку типа BOOL
 */
bind_s *
bindBool(sqltemplate_s * template, bool value){
	bind_s * bind = bindNew(template);
	bind->is_const = true;
	switch(template->driver){
		case DB_DRIVER_MYSQL:
		default:
			bind->v_const = (!value ? "0" : "1");
			bind->v_len = 1;
	}
	template->binds_len += bind->v_len;
	return bind;
}//END: bindBool



/*
 * Добавляет вставку типа INT
 */
bind_s *
bindInt(sqltemplate_s * template, int64_t value){
	bind_s * bind = bindNew(template);
	switch(template->driver){
		case DB_DRIVER_MYSQL:
		default:
			bind->v_char = intToString(value, &bind->v_len);
	}
	template->binds_len += bind->v_len;
	return bind;
}//END: bindInt



/*
 * Добавляет вставку типа DOUBLE
 * ndigits - количество знаков после запятой
 */
bind_s *
bindDouble(sqltemplate_s * template, double value, int ndigits){
	bind_s * bind = bindNew(template);
	switch(template->driver){
		case DB_DRIVER_MYSQL:
		default:
			bind->v_char = doubleToString(value, ndigits, &bind->v_len);
	}
	template->binds_len += bind->v_len;
	return bind;
}//END: bindDouble



/*
 * Добавляет вставку типа String
 */
bind_s *
bindString(sqltemplate_s * template, const char * value, size_t value_len){
	bind_s * bind = bindNew(template);
	switch(template->driver){
		case DB_DRIVER_MYSQL:
		default:
			if(!value){
				bind->is_const	= true;
				bind->v_const = "\"\"";
				bind->v_len = 2;
			}else{
				if(!value_len) value_len = strlen(value);
				bind->v_char = mysqlEscape(value, (uint32_t)value_len, true, &bind->v_len);
			}
	}
	template->binds_len += bind->v_len;
	return bind;
}//END: bindString



/*
 * Добавляет вставку типа SQL
 */
bind_s *
bindSql(sqltemplate_s * template, const char * value, size_t value_len, bool is_const){
	bind_s * bind = bindNew(template);
	bind->is_const	= is_const;
	if(!value){
		bind->is_const	= true;
		bind->v_const = "";
		bind->v_len = 0;
	}else{
		if(is_const){
			bind->v_const	= value;
			bind->v_len		= (value_len > 0 ? value_len : strlen(value));
		}else{
			bind->v_char	= (value_len > 0 ? stringCloneN(value, value_len, &bind->v_len) : stringClone(value, &bind->v_len));
		}
	}
	template->binds_len += bind->v_len;
	return bind;
}//END: bindSql



/*
 * Добавляет вставку типа DATE
 */
bind_s *
bindDate(sqltemplate_s * template, struct tm * tm){
	bind_s * bind = bindNew(template);
	struct tm ftm;
	if(!tm){
		time_t ts = time(NULL);
		tm = &ftm;
		localtime_r(&ts, tm);
	}

	switch(template->driver){
		case DB_DRIVER_MYSQL:
		default:
			bind->v_char	= mNew(14);
			bind->v_len		= strftime(bind->v_char, 14, "\"%Y-%m-%d\"", tm);
	}

	template->binds_len += bind->v_len;
	return bind;
}//END: bindDate



/*
 * Добавляет вставку типа TIME
 */
bind_s *
bindTime(sqltemplate_s * template, struct tm * tm){
	bind_s * bind = bindNew(template);
	struct tm ftm;
	if(!tm){
		time_t ts = time(NULL);
		tm = &ftm;
		localtime_r(&ts, tm);
	}

	switch(template->driver){
		case DB_DRIVER_MYSQL:
		default:
			bind->v_char	= mNew(11);
			bind->v_len		= strftime(bind->v_char, 11, "\"%H:%M:%S\"", tm);
	}

	template->binds_len += bind->v_len;
	return bind;
}//END: bindTime



/*
 * Добавляет вставку типа DATETIME
 */
bind_s *
bindDatetime(sqltemplate_s * template, struct tm * tm){
	bind_s * bind = bindNew(template);
	struct tm ftm;
	if(!tm){
		time_t ts = time(NULL);
		tm = &ftm;
		localtime_r(&ts, tm);
	}

	switch(template->driver){
		case DB_DRIVER_MYSQL:
		default:
			bind->v_char	= mNew(22);
			bind->v_len		= strftime(bind->v_char, 22, "\"%Y-%m-%d %H:%M:%S\"", tm);
	}

	template->binds_len += bind->v_len;
	return bind;
}//END: bindDatetime




/*
 * Добавляет вставку типа DATE
 */
bind_s *
bindDateT(sqltemplate_s * template, time_t ts){
	struct tm tm;
	localtime_r(&ts, &tm);
	return bindDate(template, &tm);
}//END: bindDateT



/*
 * Добавляет вставку типа TIME
 */
bind_s *
bindTimeT(sqltemplate_s * template, time_t ts){
	struct tm tm;
	localtime_r(&ts, &tm);
	return bindTime(template, &tm);
}//END: bindTimeT



/*
 * Добавляет вставку типа DATETIME
 */
bind_s *
bindDatetimeT(sqltemplate_s * template, time_t ts){
	struct tm tm;
	localtime_r(&ts, &tm);
	return bindDatetime(template, &tm);
}//END: bindDatetimeT



/*
 * Парсит SQL шаблон и возвращает готовй SQL запрос
 */
char *
sqlTemplateParse(sqltemplate_s * template, uint32_t * olen){

#define _chksize(need) do{\
	if(index + need > allocated){\
		mFree(result);\
		RETURN_ERROR(NULL,"sqlTemplateParse error: index(%u) + need(%u) > allocated(%u) memory",index,need,allocated);\
	}\
}while(0)

	if(!template || !template->query_const || !template->query_len) return NULL;
	register uint32_t index = 0;
	register uint32_t n = 0;
	register const char * ptr = template->query_const;
	uint32_t allocated = template->query_len + template->binds_len;
	char * result = mNew(allocated + 1);
	register char * rptr = result;
	bind_s * current = template->first;

	while(*ptr){
		switch(*ptr){
			case '?':
				if(!current){mFree(result);RETURN_ERROR(NULL,"sqlTemplateParse error: Failed to generate a query. Mismatch template [?] with the number of calls bind()");}
				_chksize(current->v_len);
				n = stringCopyN(rptr, current->v_const, current->v_len);
				rptr += n;
				index +=n;
				current = current->next;
			break;

			case '\r':
			case '\n':
			case '\t':
				_chksize(1);
				*rptr++ = *ptr;
				index++;
			break;

			default:
				_chksize(1);
				*rptr++ = *ptr;
				index++;
		}//switch
		ptr++;
	}//while
	*rptr = '\0';
	if(olen)*olen = index;
	return result;
}//END: sqlTemplateParse


