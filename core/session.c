/***********************************************************************
 * XG SERVER
 * core/session.c
 * Работа с сессиями
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include "globals.h"
#include "session.h"


//Настройки сессии
static session_options_s * session_options = NULL;

//Мьютекс синхронизации в момент обращения к файлам сессии
static pthread_mutex_t session_mutex_rwfile = PTHREAD_MUTEX_INITIALIZER;

//Мьютекс синхронизации в момент обращения к кешу сессии
static pthread_mutex_t session_mutex_cache = PTHREAD_MUTEX_INITIALIZER;

//Мьютекс синхронизации в момент обращения к сессии
static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

//Мьютекс синхронизации в момент обращения к IDLE списку
static pthread_mutex_t session_idle_mutex = PTHREAD_MUTEX_INITIALIZER;


//Кэш сессий
static scache_s scache;


static session_s * _session_idle_list = NULL;
static scitem_s * _scitem_idle_list = NULL;
static sccell_s * _sccell_idle_list = NULL;


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
_toIdle(_sessionToIdle, session_s, _session_idle_list, session_idle_mutex);
//static void _sessionToIdle(session_s * item){mFree(item);}

//Получает элемент из IDLE списка или создает новый, если IDLE список пуст
_fromIdle(_sessionFromIdle, session_s, _session_idle_list, session_idle_mutex);
//static session_s * _sessionFromIdle(void){return (session_s *)mNewZ(sizeof(session_s));}


//Добавляет новый/существующмй элемент в IDLE список
_toIdle(_scitemToIdle, scitem_s, _scitem_idle_list, session_idle_mutex);
//static void _scitemToIdle(scitem_s * item){mFree(item);}

//Получает элемент из IDLE списка или создает новый, если IDLE список пуст
_fromIdle(_scitemFromIdle, scitem_s, _scitem_idle_list, session_idle_mutex);
//static scitem_s * _scitemFromIdle(void){return (scitem_s *)mNewZ(sizeof(scitem_s));}


//Добавляет новый/существующмй элемент в IDLE список
_toIdle(_sccellToIdle, sccell_s, _sccell_idle_list, session_idle_mutex);
//static void _sccellToIdle(sccell_s * item){mFree(item);}

//Получает элемент из IDLE списка или создает новый, если IDLE список пуст
_fromIdle(_sccellFromIdle, sccell_s, _sccell_idle_list, session_idle_mutex);
//static sccell_s * _sccellFromIdle(void){return (sccell_s *)mNewZ(sizeof(sccell_s));}


/**
 * Инициализация session.c
 */
initialization(session_c){
	int i;
	for(i=0;i<session_idle_list_size;i++){
		_sessionToIdle(NULL);
		_scitemToIdle(NULL);
		_sccellToIdle(NULL);
	}
	DEBUG_MSG("session.c initialized.");
}//END: initialization




/***********************************************************************
 * Функции
 **********************************************************************/



/*
 * Инициализация сессий, установка опций для сессий из конфигурации
 */
void
sessionEngineInit(void){

	int i;

	if(session_options) return;
	session_options = (session_options_s * )mNewZ(sizeof(session_options_s));

	//Папка, содержащая файлы сессии
	struct stat st;
	session_options->path.ptr = fileRealpath(configRequireString("/session/path"), &session_options->path.len);
	if(!session_options->path.ptr) FATAL_ERROR("[/session/path] path not found\n");
	if(access(session_options->path.ptr, F_OK)==-1) FATAL_ERROR("Directory [%s] not found\n", session_options->path.ptr);
	if(stat(session_options->path.ptr, &st)!=0) FATAL_ERROR("Can not get info about [%s] directory\n", session_options->path.ptr);
	if(!S_ISDIR(st.st_mode)) FATAL_ERROR("[%s] is not a directory\n", session_options->path.ptr);
	if(access(session_options->path.ptr, W_OK)==-1) FATAL_ERROR("Can not write to directory [%s]\n", session_options->path.ptr);

	session_options->session_name	= stringClone(configRequireString("/session/name"), NULL);	//Название переменной в GET POST или COOKIE, отвечающая за хранение ID сессии
	session_options->timeout		= (uint32_t)configGetInt("/session/timeout", 14400);		//Таймаут между использованием сессии до истечения ее срока действия, секунд (0 - отключен)
	session_options->lifetime		= (uint32_t)configGetInt("/session/lifetime", 86400);		//Таймаут жизни сессии вне зависимости от частоты использования, секунд (0 - отключен)
	session_options->cache_limit	= (uint32_t)max(0,min(4096, configGetInt("/session/cache_limit", 128)));	//Максимальное количество сессий, которые могут быть в кэше (0 - кеш отключен)

	//Кэш сессий
	memset(&scache, '\0', sizeof(scache_s));
	scache.limit = session_options->cache_limit;
	if(scache.limit > 0){
		scache.sizeofentriesline = scache.limit * sizeof(sentry_s);
		scache.entries = (sentry_s **)mNewZ(62 * sizeof(sentry_s *));
		for(i=0;i<62;++i){
			scache.entries[i] = (sentry_s *)mNewZ(scache.sizeofentriesline);
		}
	}

}//END: sessionEngineInit



/*
 * Возвращает имя переменной сессии
 */
inline const char *
sessionGetName(void){
	return session_options->session_name;
}//END: sessionGetName



/*
 * Возвращает состояние кеша сессий
 */
inline bool
sessionCacheEnabled(void){
	return scache.limit > 0 ? true : false;
}//END: sessionCacheEnabled



/*
 * Создание новой сессии
 */
session_s *
sessionNew(const char * session_id){
	session_s * session = _sessionFromIdle();
	//session_id
	if(session_id && sessionIdIsGood(session_id)){
		stringCopyN(session->session_id, session_id, SESSION_ID_LEN);	//Копирование заданного ID сессии
	}else{
		stringRandom(SESSION_ID_LEN, session->session_id);	//Генерация ID сессии
	}
	time_t current_ts	= time(NULL);
	session->create_ts	= current_ts;
	session->open_ts	= current_ts;
	session->kv			= kvNewRoot();
	session->next		= NULL;
	BIT_SET(session->state, SESSION_CREATED);
	return session;
}//END: sessionNew



/*
 * Освобождение памяти, занятой сессией
 */
inline void
sessionFree(session_s * session){
	if(session->kv) kvFree(session->kv);
	_sessionToIdle(session);
}//END: sessionFree




/*
 * Запись значения KV в буфер
 */
static void
_sessionWriteKV(buffer_s * buffer, kv_s * current){

	uint32_t n;
	kv_s * node;
	switch(current->type){

		//NULL Null
		case KV_NULL:
		case KV_POINTER:
		case KV_FUNCTION:
		break;

		//Булево значение True / False
		case KV_BOOL:
			bufferAddHeap(buffer, (const char *)&(current->value.v_bool), sizeof(bool));
		break;

		//Целое число 123
		case KV_INT:
			bufferAddHeap(buffer, (const char *)&(current->value.v_int), sizeof(int64_t));
		break;

		//Вещественное число 123.456
		case KV_DOUBLE:
			bufferAddHeap(buffer, (const char *)&(current->value.v_double), sizeof(double));
		break;

		//Текстовое значение ""
		case KV_STRING:
			if(current->value.v_string.ptr && current->value.v_string.len > 0){
				bufferAddHeap(buffer, (const char *)&(current->value.v_string.len), sizeof(uint32_t));
				bufferAddHeap(buffer, (const char *)current->value.v_string.ptr, current->value.v_string.len);
			}else{
				bufferAddHeap(buffer, (const char *)&session_zero, sizeof(uint32_t));
			}
		break;

		//Текстовое значение JSON
		case KV_JSON:
			if(current->value.v_json.ptr && current->value.v_json.len > 0){
				bufferAddHeap(buffer, (const char *)&(current->value.v_json.len), sizeof(uint32_t));
				bufferAddHeap(buffer, (const char *)current->value.v_json.ptr, current->value.v_json.len);
			}else{
				bufferAddHeap(buffer, (const char *)&session_zero, sizeof(uint32_t));
			}
		break;

		//Дата и время
		case KV_DATETIME:
			bufferAddHeap(buffer, (const char *)&(current->value.v_datetime.ts), sizeof(time_t));
			if(current->value.v_datetime.ts > 0 && current->value.v_datetime.format){
				n = strlen(current->value.v_datetime.format);
				bufferAddHeap(buffer, (const char *)&(n), sizeof(uint32_t));
				bufferAddHeap(buffer, (const char *)current->value.v_datetime.format, n);
			}else{
				bufferAddHeap(buffer, (const char *)&session_zero, sizeof(uint32_t));
			}
		break;

		//Массив порядковый []
		case KV_ARRAY:
			bufferAddHeap(buffer, (const char *)&session_kvbegin, sizeof(uint32_t));
			node = current->value.v_list.first;
			if(node){
				for(;;){
					bufferAddHeap(buffer, (const char *)&session_zero, sizeof(uint32_t));
					bufferAddHeap(buffer, (const char *)&(node->type), sizeof(kv_t));
					_sessionWriteKV(buffer, node);
					node = node->next;
					if(!node) break;
				}
			}
			bufferAddHeap(buffer, (const char *)&session_kvend, sizeof(uint32_t));
		break;


		//Массив ассоциативный {}
		case KV_OBJECT:

			bufferAddHeap(buffer, (const char *)&session_kvbegin, sizeof(uint32_t));
			node = current->value.v_list.first;
			if(node){
				while(node){
					if(!node->key_name || !node->key_len){node = node->next; continue;}
					bufferAddHeap(buffer, (const char *)&(node->key_len), sizeof(uint32_t));
					bufferAddHeap(buffer, (const char *)node->key_name, node->key_len);
					bufferAddHeap(buffer, (const char *)&(node->key_hash), sizeof(uint32_t));
					bufferAddHeap(buffer, (const char *)&(node->type), sizeof(kv_t));
					_sessionWriteKV(buffer, node);
					node = node->next;
					if(!node) break;
				}
			}
			bufferAddHeap(buffer, (const char *)&session_kvend, sizeof(uint32_t));
		break;
	}

	return;
}//END: _sessionWriteKV



/*
 * Запись сессии в файл
 */
int
sessionSaveToFile(session_s * session){

	buffer_s * buffer = bufferCreate(session_buffer_increment);

	//Запись границы
	bufferAddHeap(buffer, (const char *)&session_boundary, sizeof(uint32_t));

	//Запись размера файла
	bufferAddHeap(buffer, (const char *)&session_zero, sizeof(uint32_t));

	//Запись размера структуры
	bufferAddHeap(buffer, (const char *)&session_s_size, sizeof(uint32_t));

	//Запись структуры session_s
	bufferAddHeap(buffer, (const char *)session, session_s_size);

	//Запись границы
	bufferAddHeap(buffer, (const char *)&session_boundary, sizeof(uint32_t));

	//Запись KV
	if(session->kv){
		_sessionWriteKV(buffer, session->kv);
	}else{
		bufferAddHeap(buffer, (const char *)&session_kvbegin, sizeof(uint32_t));
		bufferAddHeap(buffer, (const char *)&session_kvend, sizeof(uint32_t));
	}

	//Запись границы
	bufferAddHeap(buffer, (const char *)&session_boundary, sizeof(uint32_t));

	//Запись размера сессии
	bufferSeekSet(buffer, session_filesize_pos, SEEK_SET);
	bufferSetHeap(buffer, (const char *)&(buffer->count), sizeof(uint32_t));

	char * filename = mNew(session_options->path.len + SESSION_ID_LEN + 1);
	char * ptr = filename;
	ptr+=stringCopyN(ptr, session_options->path.ptr, session_options->path.len);
	*ptr++='/';
	stringCopyN(ptr, session->session_id, SESSION_ID_LEN);

	pthread_mutex_lock(&session_mutex_rwfile);
	bool result = bufferSaveToFile(buffer, filename, NULL);
	pthread_mutex_unlock(&session_mutex_rwfile);

	mFree(filename);

	bufferFree(buffer);

	//Запись буфера в файл
	if(!result) return -1;

	//Сессия сохранена
	BIT_SET(session->state, SESSION_SAVED);

	return 0;
}//END: sessionSaveToFile




/*
 * Проверяет корректность структуры данных сессии, загруженной из файла
 */
bool
sessionCheckStructure(buffer_s * buf){

	//Первая граница не найдена
	if(*(uint32_t *)&buf->buffer[session_boundary_first_pos] != session_boundary) RETURN_ERROR(false, "first boundary not found");

	//Размер буффера сессии buffer_n не соответствует размеру, записанному в буффере
	if(*(uint32_t *)&buf->buffer[session_filesize_pos] != buf->count) RETURN_ERROR(false,"buffer size != filesize"); 

	//Размер структуры session_s не соответствует размеру, записанному в буффере
	if(*(uint32_t *)&buf->buffer[session_size_pos] != session_s_size) RETURN_ERROR(false,"incorrect session_s size");

	//Граница, разделяющая структуру session_s и KV не найдена
	if(*(uint32_t *)&buf->buffer[session_boundary_middle_pos] != session_boundary) RETURN_ERROR(false,"middle boundary not found"); 

	//Первый блок [начало вложения KV] не найден
	if(*(uint32_t *)&buf->buffer[session_kvbegin_first_pos] != session_kvbegin) RETURN_ERROR(false,"first kv begin not found"); 

	//Последний блок [окончание вложения KV] не найден
	if(*(uint32_t *)&buf->buffer[buf->count - session_kvend_last_pos] != session_kvend) RETURN_ERROR(false,"last kv end not found");

	//Последняя граница не найдена
	if(*(uint32_t *)&buf->buffer[buf->count - session_boundary_last_pos] != session_boundary) RETURN_ERROR(false,"last boundary not found"); 

	return true;
}//END: sessionCheckStructure




/*
 * Чтение структуры сессии из буффера
 */
session_s *
sessionGetFromBuffer(buffer_s * buf){

	//Выделение памяти под session_s
	session_s * session = _sessionFromIdle();

	//Чтение структуры session_s
	memcpy(session, &buf->buffer[session_s_pos], session_s_size);
	session->kv		= NULL;
	session->state	= 0;
	session->cache	= 0;
	session->next	= NULL;

	return session;
}//END: sessionGetFromBuffer




/*
 * Проверка корректности ID сессии
 */
bool
sessionIdIsGood(const char * ptr){
	if(!ptr) return false;
	int n = SESSION_ID_LEN;
	while(*ptr && n > 0){
		if(!isalnum((int)*ptr)) return false;
		ptr++;
		n--;
	}
	if(n != 0) return false;
	return true;
}//END: sessionIdIsGood



/*
 * Проверяет актуальность сессии
 */
bool
sessionExpired(session_s * session){

	time_t current_ts = time(NULL);
	if(session_options->lifetime > 0 && current_ts - session->create_ts > session_options->lifetime) return true;
	if(session_options->timeout > 0 && current_ts - session->open_ts > session_options->timeout) return true;

	return false;
}//END: sessionExpired





/*
 * Чтение KV из буффера
 */
static kv_s *
_sessionReadKV(buffer_s * buf){

#define ptr_inc(size) do { \
	ptr += size; \
	if(ptr > end) goto label_error; \
}while(0);

	kv_s * root = kvNewRoot();
	uint32_t depth = 0;
	kv_s * node = root;
	kv_s * parent;
	kv_s * level[64];
	level[depth] = root;
	char * ptr = &buf->buffer[session_kvbegin_first_pos];
	char * end = ptr + buf->count - session_kvbegin_first_pos;
	uint32_t value;

	while(ptr < end){

		value = *(uint32_t *)ptr;

		if(value == session_boundary) goto label_end;
		ptr_inc(sizeof(uint32_t));

		if(value == session_kvbegin){depth++; level[depth] = node; parent=node; continue;}
		if(value == session_kvend){depth--; parent=level[depth]; continue;}


		if(value > 0){
			node = kvAppend(parent, ptr, value, KV_REPLACE);
			ptr_inc(value);
			node->key_hash = *(uint32_t *)ptr; 
			ptr_inc(sizeof(uint32_t));
		}else{
			node = kvAppend(parent, NULL, 0, true);
		}

		//тип переменной
		node->type = *(kv_t *)ptr; 
		ptr_inc(sizeof(kv_t));
		switch(node->type){

			case KV_BOOL:
				node->value.v_bool = *(bool *)ptr; ptr_inc(sizeof(bool));
			break;
			case KV_INT:
				node->value.v_int = *(int64_t *)ptr; ptr_inc(sizeof(int64_t));
			break;
			case KV_DOUBLE:
				node->value.v_double = *(double *)ptr; ptr_inc(sizeof(double));
			break;
			case KV_STRING:
				value = *(uint32_t *)ptr; ptr_inc(sizeof(uint32_t));
				if(value > 0){
					node->value.v_string.ptr = stringCloneN(ptr, value, &(node->value.v_string.len));
					ptr_inc(value);
				}
			break;
			case KV_JSON:
				value = *(uint32_t *)ptr; ptr_inc(sizeof(uint32_t));
				if(value > 0){
					node->value.v_json.ptr = stringCloneN(ptr, value, &(node->value.v_json.len));
					ptr_inc(value);
				}
			break;
			case KV_DATETIME:
				node->value.v_datetime.ts = *(time_t *)ptr; ptr_inc(sizeof(time_t));
				value = *(uint32_t *)ptr; ptr_inc(sizeof(uint32_t));
				if(value > 0){
					node->value.v_string.ptr = stringCloneN(ptr, value, &(node->value.v_string.len));
					ptr_inc(value);
				}
			break;
			default:
			break;
		}//тип переменной

	}//while

	label_error:

	//Внутренняя ошибка при чтении файла сессии
	kvFree(root);
	root = NULL;

	label_end:

	return root;
}//END: _sessionReadKV





/*
 * Чтение сессии из файла
 */
session_s *
sessionLoadFromFile(const char * session_id){

	buffer_s * buf;
	//Чтение сессии из файла в буффер
	char * filename = mNew(session_options->path.len + SESSION_ID_LEN + 1);
	char * ptr = filename;
	ptr+=stringCopyN(ptr, session_options->path.ptr, session_options->path.len);
	*ptr++='/';
	stringCopyN(ptr, session_id, SESSION_ID_LEN);

	pthread_mutex_lock(&session_mutex_rwfile);
	buf = bufferLoadFromFile(filename, NULL);
	pthread_mutex_unlock(&session_mutex_rwfile);

	mFree(filename);

	if(!buf) return NULL;

	//Проверка структуры сессии
	if(!sessionCheckStructure(buf)){bufferFree(buf); return NULL;}

	//Получение сессии
	session_s * session = sessionGetFromBuffer(buf);

	//Если срок действия сессии истек, возвращаем NULL
	if(sessionExpired(session)){
		bufferFree(buf);
		sessionFree(session);
		return NULL;
	}

	//Получение KV сессии
	if((session->kv = _sessionReadKV(buf)) == NULL){
		bufferFree(buf);
		sessionFree(session);
		return NULL;
	}

	//Очистка буффера
	bufferFree(buf);

	//Сессия загружена
	BIT_SET(session->state, SESSION_LOADED);
	session->open_ts = time(NULL);

	return session;
}//END: sessionLoad









/************************************
 * Функции записи переменных в сессию
 ************************************/

inline bool
sessionSetKV(session_s * session, kv_s * kv, const char * key_name, uint32_t key_len){
	if(!session) return false;
	pthread_mutex_lock(&session_mutex);
	if(!session->kv) session->kv = kvNewRoot();
	if(key_name) kvSetKey(kv, key_name, key_len);
	kvInsert(session->kv, kv, KV_REPLACE);
	BIT_SET(session->state, SESSION_CHANGED);
	pthread_mutex_unlock(&session_mutex);
	return true;
}//END: sessionSetKV





inline bool
sessionSetBool(session_s * session, const char * path, bool value){
	if(!session) return false;
	pthread_mutex_lock(&session_mutex);
	if(!session->kv) session->kv = kvNewRoot();
	kvSetBoolByPath(session->kv, path, value);
	BIT_SET(session->state, SESSION_CHANGED);
	pthread_mutex_unlock(&session_mutex);
	return true;
}//END: sessionSetBool



inline bool
sessionSetInt(session_s * session, const char * path, int64_t value){
	if(!session) return false;
	pthread_mutex_lock(&session_mutex);
	if(!session->kv) session->kv = kvNewRoot();
	kvSetIntByPath(session->kv, path, value);
	BIT_SET(session->state, SESSION_CHANGED);
	pthread_mutex_unlock(&session_mutex);
	return true;
}//END: sessionSetInt



inline bool
sessionSetDouble(session_s * session, const char * path, double value){
	if(!session) return false;
	pthread_mutex_lock(&session_mutex);
	if(!session->kv) session->kv = kvNewRoot();
	kvSetDoubleByPath(session->kv, path, value);
	BIT_SET(session->state, SESSION_CHANGED);
	pthread_mutex_unlock(&session_mutex);
	return true;
}//END: sessionSetDouble


inline bool
sessionSetString(session_s * session, const char * path, const char * str, uint32_t len){
	if(!session) return false;
	pthread_mutex_lock(&session_mutex);
	if(!session->kv) session->kv = kvNewRoot();
	kvSetStringByPath(session->kv, path,  str, len);
	BIT_SET(session->state, SESSION_CHANGED);
	pthread_mutex_unlock(&session_mutex);
	return true;
}//END: sessionSetString



inline bool
sessionSetStringPtr(session_s * session, const char * path, char * str, uint32_t len){
	if(!session) return false;
	pthread_mutex_lock(&session_mutex);
	if(!session->kv) session->kv = kvNewRoot();
	kvSetStringPtrByPath(session->kv, path, str, len);
	BIT_SET(session->state, SESSION_CHANGED);
	pthread_mutex_unlock(&session_mutex);
	return true;
}//END: sessionSetStringPtr



inline bool
sessionSetDatetime(session_s * session, const char * path, time_t ts, const char * format){
	if(!session) return false;
	pthread_mutex_lock(&session_mutex);
	if(!session->kv) session->kv = kvNewRoot();
	kvSetDatetimeByPath(session->kv, path, ts, format);
	BIT_SET(session->state, SESSION_CHANGED);
	pthread_mutex_unlock(&session_mutex);
	return true;
}//END: sessionSetDatetime




/*************************************
 * Функции чтения переменных из сессии
 *************************************/


bool
sessionGetBool(session_s * session, const char * path, bool def){
	if(!session) return def;
	pthread_mutex_lock(&session_mutex);
	bool result = kvGetBoolByPath(session->kv, path, def);
	pthread_mutex_unlock(&session_mutex);
	return result;
}//END: sessionGetBool



int64_t
sessionGetInt(session_s * session, const char * path, int64_t def){
	if(!session) return def;
	pthread_mutex_lock(&session_mutex);
	int64_t result = kvGetIntByPath(session->kv, path, def);
	pthread_mutex_unlock(&session_mutex);
	return result;
}//END: sessionGetInt



double
sessionGetDouble(session_s * session, const char * path, double def){
	if(!session) return def;
	pthread_mutex_lock(&session_mutex);
	double result = kvGetDoubleByPath(session->kv, path, def);
	pthread_mutex_unlock(&session_mutex);
	return result;
}//END: sessionGetDouble


/*
 * Возвращает указатель на текстовое значение переменной сессии 
 * небезопасно в многопоточной среде, поскольку несколько потоков могут одновременно работать с обной и ой же сессией
 * Рекументуется использовать sessionGetString()
 */
const char *
sessionGetStringPtr(session_s * session, const char * path, const char * def){
	if(!session) return def;
	pthread_mutex_lock(&session_mutex);
	const char * result = kvGetStringByPath(session->kv, path, def);
	pthread_mutex_unlock(&session_mutex);
	return result;
}//END: sessionGetStringPtr



/*
 * Возвращает указатель на текстовое значение переменной сессии в структуре const_string_s
 * небезопасно в многопоточной среде, поскольку несколько потоков могут одновременно работать с обной и ой же сессией
 * Рекументуется использовать sessionGetStringS()
 */
const_string_s *
sessionGetStringSPtr(session_s * session, const char * path, const_string_s * def){
	if(!session) return def;
	pthread_mutex_lock(&session_mutex);
	const_string_s * result = kvGetStringSByPath(session->kv, path, def);
	pthread_mutex_unlock(&session_mutex);
	return result;
}//END: sessionGetStringSPtr



/*
 * Возвращает текстовое значение переменной сессии
 * если переменная по заданному ключу не найдена - возвращаем NULL
 */
char *
sessionGetString(session_s * session, const char * path, uint32_t * olen){
	if(!session) return NULL;
	char * result = NULL;
	pthread_mutex_lock(&session_mutex);
	const_string_s * data = kvGetStringSByPath(session->kv, path, NULL);
	if(data){
		result = stringCloneN(data->ptr, data->len, olen);
	}
	pthread_mutex_unlock(&session_mutex);
	return result;
}//END: sessionGetString



/*
 * Возвращает текстовое значение переменной сессии в структуре string_s
 * если переменная по заданному ключу не найдена - возвращаем NULL
 */
string_s *
sessionGetStringS(session_s * session, const char * path){
	if(!session) return NULL;
	string_s * result = NULL;
	pthread_mutex_lock(&session_mutex);
	const_string_s * data = kvGetStringSByPath(session->kv, path, NULL);
	if(data){
		result = (string_s *)mNewZ(sizeof(string_s));
		result->ptr = stringCloneN(data->ptr, data->len, &result->len);
	}
	pthread_mutex_unlock(&session_mutex);
	return result;
}//END: sessionGetStringS



/*
 * Возвращает текстовое значение переменной сессии в буфер buffer_s
 * если переменная по заданному ключу не найдена - возвращаем NULL
 */
buffer_s *
sessionGetBuffer(session_s * session, const char * path, buffer_s * buf){
	if(!session) return NULL;
	pthread_mutex_lock(&session_mutex);
	const_string_s * data = kvGetStringSByPath(session->kv, path, NULL);
	if(data){
		if(!buf) buf = bufferCreate(0);
		bufferAddStringN(buf, data->ptr, data->len);
	}else{
		buf = NULL;	//если 
	}
	pthread_mutex_unlock(&session_mutex);
	return buf;
}//END: sessionGetBuffer




/*
 * Удаление файла сессии
 */
bool
sessionFileDelete(const char * session_id){
	char * filename = mNew(session_options->path.len + SESSION_ID_LEN + 1);
	char * ptr = filename;
	ptr+=stringCopyN(ptr, session_options->path.ptr, session_options->path.len);
	*ptr++='/';
	stringCopyN(ptr, session_id, SESSION_ID_LEN);
	pthread_mutex_lock(&session_mutex_rwfile);
	bool result = (unlink(filename) == 0 ? true : false);
	pthread_mutex_unlock(&session_mutex_rwfile);
	mFree(filename);
	return result;
}//END: sessionFileDelete








/***********************************************************************
 * Session cache
 **********************************************************************/

static inline sentry_s *
scGetEntryPointer(uint32_t cindex, uint32_t hash){
	return (sentry_s *)&scache.entries[cindex][(hash % scache.limit)];
}//END: scGetEntryPointer


/*
 * Удаление элемента из списка кеша сессий
 */
static void
scDelete(scitem_s * item){
	if(item->prev) item->prev->next = item->next;
	if(item->next) item->next->prev = item->prev;
	if(scache.list.last == item) scache.list.last = item->prev;
	if(scache.list.first == item) scache.list.first = item->next;
	scache.list.count--;
	if(!item->session || item->session->cache != item || !item->cell) FATAL_ERROR("!item->session || item->session->cache != item || !item->cell");
	//Удаление элемента хеш таблицы кеша сессий
	if(item->cell->prev) item->cell->prev->next = item->cell->next;
	if(item->cell->next) item->cell->next->prev = item->cell->prev;
	if(item->cell->entry->first == item->cell) item->cell->entry->first =  item->cell->next;
	_sccellToIdle(item->cell);
	//Сохранение сессии
	sessionSaveToFile(item->session);
	//Удаление сессии
	sessionFree(item->session);
	_scitemToIdle(item);
}//END: scDelete




/*
 * Добавление нового элемента в список кеша сессий
 */
static scitem_s *
scAdd(session_s * session){
	scitem_s * item = _scitemFromIdle();
	item->session	= session;
	item->using		= 0;
	session->cache	= item;
	BIT_SET(session->state, SESSION_CACHED);
	if(scache.list.first){
		item->next = scache.list.first;
		scache.list.first->prev = item;
	}
	if(!scache.list.last) scache.list.last = item;
	scache.list.first = item;
	scache.list.count++;

	//Создание элемента хеш таблицы кеша сессий
	uint32_t hash = hashString(session->session_id, NULL);
	sccell_s * cell = _sccellFromIdle();
	cell->item = item;
	item->cell = cell;
	cell->entry = scGetEntryPointer((uint32_t)_char_index[(int)session->session_id[0]] , hash);
	if(cell->entry->first){
		cell->next = cell->entry->first;
		cell->entry->first->prev = cell;
	}
	cell->entry->first = cell;


	//Если количество элементов в списке более чем scache.limit, удаляем последний элемент
	if(scache.list.count > scache.limit){
		scitem_s * d;
		for(d = scache.list.last; d != NULL && d != item; d = d->prev){
			if(d->using == 0){
				scDelete(d);
				return item;
			}
		}
	}
	return item;
}//END: scAdd



/*
 * Ставит элемент списка кеша сессий на первое место в кеше
 */
static scitem_s *
scFirst(scitem_s * item){
	if(scache.list.first == item) return item;
	if(item->prev) item->prev->next = item->next;
	if(item->next) item->next->prev = item->prev;
	if(scache.list.last == item) scache.list.last = item->prev;
	item->next = scache.list.first;
	scache.list.first->prev = item;
	scache.list.first = item;
	return item;
}//END: scFirst



/*
 * Добавляет сессию в кеш сессий
 */
bool
sessionCacheSave(session_s * session){
	if(!session || !session->session_id || !sessionCacheEnabled()) return false;

	pthread_mutex_lock(&session_mutex_cache);

	//Сессия уже находится в кеше - уменьшаем счетчик использований на 1
	if(session->cache){
		session->cache->using--;
	}
	//Сессия не в кеше - добавляем в кеш
	else{
		scAdd(session);
	}

	pthread_mutex_unlock(&session_mutex_cache);

	return true;
}//END: sessionCacheSave



/*
 * Сохранение на диск всех сессий, находящихся в кеше
 */
void
sessionCacheSaveAll(void){
	if(!sessionCacheEnabled()) return;
	scitem_s * item;
	pthread_mutex_lock(&session_mutex_cache);
	for(item = scache.list.first; item != NULL; item = item->next){
		if(item->session) sessionSaveToFile(item->session);
	}
	pthread_mutex_unlock(&session_mutex_cache);
	return;
}//END: sessionCacheSaveAll




/*
 * Извлекает сессию из кеша сессий
 */
session_s *
sessionCacheLoad(const char * session_id){

	if(!session_id || !sessionCacheEnabled()) return NULL;
	uint32_t hash = hashString(session_id, NULL);
	sentry_s * entry = scGetEntryPointer((uint32_t)_char_index[(int)session_id[0]] , hash);
	if(!entry) FATAL_ERROR("entry is NULL");
	sccell_s * cell;
	session_s * session = NULL;

	pthread_mutex_lock(&session_mutex_cache);

	//Просмотр кеша и поиск нужной сессии по session_id
	for(cell = entry->first; cell != NULL; cell = cell->next){
		if(stringCompare(cell->item->session->session_id, session_id)){
			session = cell->item->session;
			scFirst(cell->item); //На первую позицию в списке кеша сессий
			cell->item->using++; //то же, что и session->cache->using++;
			//Снимаем биты, что сессия измененена, сохранена и создана
			BIT_UNSET(session->state, SESSION_CHANGED);
			BIT_UNSET(session->state, SESSION_SAVED);
			BIT_UNSET(session->state, SESSION_CREATED);
			//Устанавливаем бит, что сессия кеширована
			BIT_SET(session->state, SESSION_CACHED);
			//Устанавливаем новое время открытия сессии
			session->open_ts = time(NULL);
			break;
		}
	}

	pthread_mutex_unlock(&session_mutex_cache);

	return session;
}//END: sessionCacheLoad






/*
 * Сохранение сессии (в файл или кеш) и ее закрытие
 */
void
sessionClose(session_s * session){

	if(!session || !session->session_id) return;
	/*
	buffer_s * b = kvEcho(session->kv, KVF_JSON, NULL);
	bufferPrint(b);
	bufferFree(b);
	*/

	pthread_mutex_lock(&session_mutex);

	//Кеш сессий отключен
	if(!sessionCacheEnabled()){
		if(BIT_ISSET(session->state, SESSION_CHANGED)) sessionSaveToFile(session);
		sessionFree(session);
		pthread_mutex_unlock(&session_mutex);
		return;
	}

	//Сохранение сессии в кеш, если сессия уже в кеше или если она была изменена
	if(BIT_ISSET(session->state, SESSION_CHANGED) || BIT_ISSET(session->state, SESSION_CACHED)){
		sessionCacheSave(session);
	}else{
		sessionFree(session);
	}

	pthread_mutex_unlock(&session_mutex);

	return;
}//END: sessionClose




/*
 * Проверяет сессию на принадлежность указанному пользователю
 */
bool
sessionIsValidClient(session_s * session, socket_addr_s * sa, uint32_t user_agent){

	//Если сессия была только что создана, то она валидна по-умолчанию
	if(BIT_ISSET(session->state, SESSION_CREATED)){
		memcpy(&session->ip_addr, sa, sizeof(socket_addr_s));
		session->user_agent = user_agent;
		return true;
	}
	if(!ipCompare(&session->ip_addr, sa)) return false;
	if(session->user_agent != user_agent) return false;
	return true;
}//END: sessionIsValidUser





/*
 * Загрузка существующей сессии из файла сессии или старт новой сессии 
 */
session_s *
sessionStart(const char * session_id){

	if(!session_id || !sessionIdIsGood(session_id)) return sessionNew(NULL);
	session_s * session = NULL;

	pthread_mutex_lock(&session_mutex);

	//Попытка чтения сессии из кеша сессий
	if(sessionCacheEnabled()){
		session = sessionCacheLoad(session_id);
	}
	if(!session){
		session = sessionLoadFromFile(session_id);
	}

	if(!session) session = sessionNew(NULL);

	pthread_mutex_unlock(&session_mutex);

	return session;
}//END: sessionStart




/***********************************************************************
 * Session cleaner - сборщик мусора (удаление старых сессий)
 **********************************************************************/


/*
 * Удаляет истекшие по времени действия файлы сессий
 */
void
sessionDeleteExpired(void){

	//printf("sessionDeleteExpired() is executing on %u...\n", (uint32_t)time(NULL));

	struct dirent * entry;
	const char * d_name;
	bool need_delete = false;
	DIR * d = opendir(session_options->path.ptr);
	if (!d) return;

	char * filename = mNew(session_options->path.len + SESSION_ID_LEN + 1);
	char * fname = filename;
	fname+=stringCopyN(fname, session_options->path.ptr, session_options->path.len);
	*fname++='/';
	/*
	buffer_s * buf;
	session_s * session;
	*/
	struct stat st;
	time_t current_ts = time(NULL);


	while((entry = readdir(d))!=NULL){
		d_name = entry->d_name;

		if (!(entry->d_type & DT_DIR)){

			if(!sessionIdIsGood(d_name)) continue;

			stringCopyN(fname, d_name, SESSION_ID_LEN);

			need_delete = false;

			if(fileStat(&st, filename)){
				if(session_options->lifetime > 0 && current_ts - st.st_mtime > session_options->lifetime) need_delete = true;
				if(session_options->timeout > 0 && current_ts - st.st_mtime > session_options->timeout) need_delete = true;
			}else{
				need_delete = true;
			}
/*
			if(!need_delete){

				pthread_mutex_lock(&session_mutex_rwfile);
				buf = bufferLoadFromFile(filename, NULL);
				pthread_mutex_unlock(&session_mutex_rwfile);

				if(buf){
					if(!sessionCheckStructure(buf)){
						need_delete = true;
					}else{
						session = sessionGetFromBuffer(buf);
						if(sessionExpired(session)) need_delete = true;
					}
					bufferFree(buf);
				}else{
					need_delete = true;
				}

			}
*/
			if(need_delete == true){
				DEBUG_MSG("Session file [%s] is expired, delete it\n",filename);
				pthread_mutex_lock(&session_mutex_rwfile);
				unlink(filename);
				pthread_mutex_unlock(&session_mutex_rwfile);
			}
		}

	}
	closedir(d);
	mFree(filename);
	return;
}//END: sessionDeleteExpired
























