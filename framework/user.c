/***********************************************************************
 * XG SERVER
 * framework/user.c
 * Работа с пользователями: аутентификация, добавление, удаление, получение сведений
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   

#include <openssl/sha.h>
#include "core.h"
#include "kv.h"
#include "globals.h"
#include "server.h"
#include "db.h"
#include "user.h"
#include "language.h"


static pthread_mutex_t user_mutex = PTHREAD_MUTEX_INITIALIZER;

//Мьютекс синхронизации в момент обращения к IDLE списку
static pthread_mutex_t user_idle_mutex = PTHREAD_MUTEX_INITIALIZER;
static user_s * _user_idle_list = NULL;
static userlist_s _ulist;


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
_toIdle(_userToIdle, user_s, _user_idle_list, user_idle_mutex);

//Получает элемент из IDLE списка или создает новый, если IDLE список пуст
_fromIdle(_userFromIdle, user_s, _user_idle_list, user_idle_mutex);


//Функция-обработчик события EVENT_LOADER_COMPLETE (Событие функции main() после обработки конфигурационных файлов
static void _ev_user_c(evinfo_s * event){
	userlistLoadFromDB();
}



/**
 * Инициализация user.c
 */
initialization(user_c){
	memset(&_ulist, '\0',sizeof(userlist_s));
	addListener(EVENT_LOADER_COMPLETE, _ev_user_c, true, 0);
	DEBUG_MSG("user.c initialized.");
}//END: initialization


/*
 * KV типа KV_OBJECT, где перечислены допустимые названия полей таблицы USER_TABLE_NAME
 */
static kv_s * db_table_user = NULL;



/***********************************************************************
 * Функции работы со структурой пользователя
 **********************************************************************/

//Получение структуры пользователя
inline user_s * userStructGet(void){
	return _userFromIdle();
}

//Освобождение структуры пользователя
inline void userStructFree(void * ptr){
	if(!ptr) return;
	user_s * user = (user_s *)ptr;
	if(user->info) kvFree(user->info);
	_userToIdle((user_s *)user);
}




/***********************************************************************
 * Функции работы со структурой пользователя
 **********************************************************************/

//Применяет настройки к пользователю из конфигурационных файлов
static user_s * _configUserAttr(user_s * user){

	if(!user) return NULL;

	//Массив настроек пользователей
	kv_s * users_kv = kvSearch(XG_CONFIG, CONST_STR_COMMA_LEN("users"));
	if(!users_kv) return user;
	//Настройки пользователя
	kv_s * user_kv = kvSearch(users_kv, user->login, user->login_n);
	if(!user_kv || user_kv->type != KV_OBJECT) return user;

	char * ptr;
	kv_s * node;
	for(node = user_kv->value.v_list.first; node != NULL; node = node->next){
		ptr = node->key_name;
#ifdef KV_KEY_NAME_IS_DYNAMIC
			if(!ptr) continue;
#else
			if(!*ptr) continue;
#endif
		switch(tolower((int)*ptr)){

			//access_level
			case 'a':
				if(node->type == KV_INT && stringCompareCaseN(node->key_name, CONST_STR_COMMA_LEN("access_level"))) user->access_level = (uint32_t)node->value.v_int;	//access_level
			break;

			//status
			case 's':
				if(node->type == KV_INT && stringCompareCaseN(ptr, CONST_STR_COMMA_LEN("status"))) user->status = (uint32_t)node->value.v_int;
			break;

			//is_super
			case 'i':
				if(node->type == KV_BOOL && stringCompareCaseN(ptr, CONST_STR_COMMA_LEN("is_super"))) user->is_super = node->value.v_bool;
			break;

		} //switch

	}//for

	return user;
}//END:_configUserAttr



//Заполняет структуру user_s данными из KV
static user_s * _fillUserStruct(user_s * user, kv_s * kv){

	if(!user) user = _userFromIdle();
	kv_s * node;
	char * ptr;

	for(node = kv->value.v_list.first; node != NULL; node = node->next){
		ptr = node->key_name;
#ifdef KV_KEY_NAME_IS_DYNAMIC
			if(!ptr) continue;
#else
			if(!*ptr) continue;
#endif
		switch(tolower((int)*ptr)){

			//account_id, access_level
			case 'a':
				if(stringCompareCaseN(node->key_name, CONST_STR_COMMA_LEN(USER_TABLE_ACCOUNT_ID))){
					if(node->type == KV_INT){
						user->account_id = (uint32_t)node->value.v_int;
					}else
					if(node->type == KV_STRING){
						user->account_id = (uint32_t)atol(node->value.v_string.ptr);
					}
				}
				else
				if(stringCompareCaseN(node->key_name, CONST_STR_COMMA_LEN(USER_TABLE_AL))){
					if(node->type == KV_INT){
						user->access_level = (uint32_t)node->value.v_int;
					}else
					if(node->type == KV_STRING){
						user->access_level = (uint32_t)atol(node->value.v_string.ptr);
					}
				}
			break;

			//login, language
			case 'l':
				switch(tolower((int)*(ptr+1))){
					//language
					case 'a':
						if(stringCompareCaseN(ptr, CONST_STR_COMMA_LEN(USER_TABLE_LANGUAGE))){
							if(langExists(node->value.v_string.ptr, node->value.v_string.len)){
								user->language_n = stringCopyN(user->language, node->value.v_string.ptr, USER_LANGUAGE_LEN);
							}else{
								user->language_n = stringCopyN(user->language, langDefault(), USER_LANGUAGE_LEN);
							}
						}
					break;
					//login
					case 'o':
						if(stringCompareCaseN(ptr, CONST_STR_COMMA_LEN(USER_TABLE_LOGIN))) user->login_n = stringCopyN(user->login, node->value.v_string.ptr, USER_LOGIN_LEN);
					break;
				}
			break;

			//password
			case 'p':
				if(stringCompareCaseN(ptr, CONST_STR_COMMA_LEN(USER_TABLE_PASSWORD))) user->password_n = stringCopyN(user->password, node->value.v_string.ptr, USER_PASSWORD_HASH_LEN);
			break;

			//status
			case 's':
				if(stringCompareCaseN(ptr, CONST_STR_COMMA_LEN(USER_TABLE_STATUS))){
					if(node->type == KV_INT){
						user->status = (uint32_t)node->value.v_int;
					}else
					if(node->type == KV_STRING){
						user->status = (uint32_t)atol(node->value.v_string.ptr);
					}
				}
			break;

			//user_id
			case 'u':
				if(stringCompareCaseN(ptr, CONST_STR_COMMA_LEN(USER_TABLE_USER_ID))){
					if(node->type == KV_INT){
						user->user_id = (uint32_t)node->value.v_int;
					}else
					if(node->type == KV_STRING){
						user->user_id = (uint32_t)atol(node->value.v_string.ptr);
					}
				}
			break;

		} //switch

	}//for

	return user;
}//END: _fillUserStruct




/***********************************************************************
 * Функции работы со списком пользователей
 **********************************************************************/

/*
 * Загрузка списка пользователей из базы данных
 */
result_e
userlistLoadFromDB(void){

	//Данная функция вызывается один раз при старте сервера
	if(db_table_user) return RESULT_OK;

	db_table_user = kvGetByPath(XG_CONFIG, "database/"DB_INSTANCE_MAIN"/tables/"USER_TABLE_NAME);
	if(!db_table_user) FATAL_ERROR("Config setting of user table not found by path: database/"DB_INSTANCE_MAIN"/tables/"USER_TABLE_NAME);

	mysql_s * db = mysqlGetInstance(XG_MYSQL_INSTANCES, DB_INSTANCE_MAIN);
	if(!db) RETURN_ERROR(RESULT_ERROR, "!db");
	MYSQL_ROW row;

	//Поличесние минимального ID, максимального ID и количества пользователей из базы данных
	if(!mysqlQuery(db, CONST_STR_COMMA_LEN("SELECT count(*), min(`"USER_TABLE_USER_ID"`), max(`"USER_TABLE_USER_ID"`) FROM `"USER_TABLE_NAME"` WHERE 1"))) RETURN_ERROR(RESULT_ERROR, "!mysqlQuery: min, max, count");
	if(!mysqlStoreResult(db)) RETURN_ERROR(RESULT_ERROR, "!mysqlStoreResult");
	if(!db->rows_count) RETURN_ERROR(RESULT_OK, "!db->rows_count");
	if((row = mysqlFetchRow(db))==NULL) RETURN_ERROR(RESULT_ERROR, "!mysqlFetchRow");

	mysqlAsUInt32(db, 0, &_ulist.count);
	mysqlAsUInt32(db, 1, &_ulist.min_id);
	mysqlAsUInt32(db, 2, &_ulist.max_id);
	if(!_ulist.count) RETURN_ERROR(RESULT_OK, "No user accounts");

	//Динамический список для структур user_s
	_ulist.list = darrayCreate(DATYPE_POINTER, _ulist.count + 256, 256);
	darraySetOffset(_ulist.list, _ulist.min_id);	//Приравниваем минимальный ID из базы данных к индексу = 0
	darraySetFree(_ulist.list, userStructFree);		//Устанавливаем функцию, освобождающаю память, занятую структурой user_s
	_ulist.login = streeNew(NULL);

	//Создание структур user_s
	int i;
	for(i=0;i<(_ulist.count + 256);i++) _userToIdle(NULL);

	//Загружаем список пользователей из базы данных в структуры user_s
	if(!mysqlQuery(db, CONST_STR_COMMA_LEN("SELECT * FROM `"USER_TABLE_NAME"` WHERE `"USER_TABLE_DELETED"`=0"))) RETURN_ERROR(RESULT_ERROR, "!mysqlQuery: select all users");
	if(!mysqlUseResult(db)) RETURN_ERROR(RESULT_ERROR, "!mysqlUseResult");


	kv_s * kv_node;
	kv_s * kv_tmp = kvCopy(NULL, db_table_user);

	//Проверяем список полей таблицы USER_TABLE_NAME
	for (i = 0; i < db->fields_count; i++){
		if((kv_node = kvSearch(kv_tmp, db->fields[i].name, db->fields[i].name_len))==NULL) FATAL_ERROR("Field [%s] not found by path: database/"DB_INSTANCE_MAIN"/tables/"USER_TABLE_NAME, db->fields[i].name);
		if(kv_node->type == KV_POINTER || kv_node->type == KV_FUNCTION || kv_node->type == KV_OBJECT || kv_node->type == KV_ARRAY) FATAL_ERROR("Incorrect field [%s] type (only: null, bool, int, double, string) by path: database/"DB_INSTANCE_MAIN"/tables/"USER_TABLE_NAME, db->fields[i].name);
		kvFree(kv_node);
	}
	if(kv_tmp->value.v_list.first != NULL){
		for(kv_node = kv_tmp->value.v_list.first; kv_node != NULL; kv_node = kv_node->next){
			ERROR_MSG("Field [%s] not exists in database table, but is set by path: database/"DB_INSTANCE_MAIN"/tables/"USER_TABLE_NAME, kv_node->key_name);
		}
		FATAL_ERROR("Incorrect settings by path: database/"DB_INSTANCE_MAIN"/tables/"USER_TABLE_NAME);
	}
	kvFree(kv_tmp);

	user_s * user;

	//Чтение всех запиcей из таблицы USER_TABLE_NAME в структуры user_s
	while((row = mysqlFetchRow(db))!=NULL){
		user = userStructGet();
		user->info = mysqlRowAsKV(db, ROWAS_OBJECT, NULL);
		_fillUserStruct(user, user->info);
		_configUserAttr(user);
		darraySetId(_ulist.list, user->user_id, user);
		streeSet(_ulist.login, user->login, user, SNODE_REPLACE);
		//printf("user_id = [%u], status=[%u], login = [%s], password = [%s]\n", user->user_id, user->status, user->login, user->password);
	}//while row

	mysqlFreeResult(db);


	//streePrint(_ulist.login);

	return RESULT_OK;
}//END: userlistLoadFromDB






/***********************************************************************
 * Функции пользователь <--> сессия
 **********************************************************************/


/*
 * Получение структуры данных пользователя из ID пользователя
 */
user_s * 
userFromId(uint32_t user_id){
	if(!user_id) return NULL;
	user_s * user = (user_s *)darrayGetPointer(_ulist.list, user_id, NULL);
	return user;
}//END: userFromId



/*
 * Получение структуры данных пользователя из логина
 */
user_s * 
userFromLogin(const char * login){
	if(!login || !*login) return NULL;
	snode_s * snode = streeGet(_ulist.login, login);
	if(!snode || !snode->data) return NULL;
	user_s * user = (user_s *)snode->data;
	return user;
}//END: userFromLogin






/*
 * Получение структуры данных пользователя из сессии
 */
user_s * 
userFromSession(session_s * session){
	if(!session || !session->user_id) return NULL;
	user_s * user = (user_s *)darrayGetPointer(_ulist.list, session->user_id, NULL);
	return user;
}//END: userFromSession




/*
 * Запись структуры данных пользователя в сессию
 */
bool
userToSession(user_s * user){
	if(!user) return false;
	/*
	if(!user->session) RETURN_ERROR(false, "user->session is NULL!");
	sessionSetInt(user->session, USER_SESSION_NAME"/"USER_TABLE_USER_ID, (int64_t)user->user_id);
	sessionSetInt(user->session, USER_SESSION_NAME"/"USER_TABLE_STATUS, (int64_t)user->status);
	sessionSetInt(user->session, USER_SESSION_NAME"/"USER_TABLE_ACCOUNT_ID, (int64_t)user->account_id);
	sessionSetInt(user->session, USER_SESSION_NAME"/"USER_TABLE_AL, (int64_t)user->access_level);
	sessionSetString(user->session, USER_SESSION_NAME"/"USER_TABLE_LANGUAGE, user->language, user->language_n);
	sessionSetString(user->session, USER_SESSION_NAME"/"USER_TABLE_LOGIN, user->login, user->login_n);
	*/
	return true;
}//END: userToSession





/***********************************************************************
 * Аутентификация пользователя
 **********************************************************************/

/*
 * Возвращает текстовое описание статуса аутентификации на указанном языке
 * auth - статус аутентификации
 * language - требуемый язык сообщения (или NULL для исползования заданного по-умолчанию)
 */
const char * 
userAuthToString(auth_state_e auth, const char * language){
	language = langSelect(language, 0);
	switch(auth){

		//Пользователь не аутентифицирован
		case AUTH_NONE:
			return lang("/users/login/auth/none", language);
		break;

		//Ошибка аутентификации (какая-то внутренняя проблема: нет связи с СУБД и т.д.) - ошибка сервера 500
		case AUTH_ERROR:
			return lang("/users/login/auth/error", language);
		break;

		//Логин или пароль не заданы
		case AUTH_LP_EMPTY:
			return lang("/users/login/auth/empty", language);
		break;

		//Неверно указаны логин или пароль
		case AUTH_LP_INCORRECT:
			return lang("/users/login/auth/incorrect", language);
		break;

		//Пользователь с указанным логином не существует
		case AUTH_USER_EXISTS:
			return lang("/users/login/auth/exists", language);
		break;

		//Учетная запись пользователя заблокирована
		case AUTH_USER_LOCKED:
			return lang("/users/login/auth/ulocked", language);
		break;

		//Учетная запись пользователя не привязана к какому-либо счету
		case AUTH_USER_UNTIED:
			return lang("/users/login/auth/untied", language);
		break;

		//Счет заблокирован
		case AUTH_ACCOUNT_LOCKED:
			return lang("/users/login/auth/alocked", language);
		break;

		//Доступ к учетной записи запрещен при текущих условиях входа, при этом учетная запись не заблокирована (например, по IP адресу)
		case AUTH_DENIED:
			return lang("/users/login/auth/denied", language);
		break;

		//Время сессии истекло
		case AUTH_EXPIRED:
			return lang("/users/login/auth/expired", language);
		break;

		//Все ок
		case AUTH_OK:
			return lang("/users/login/auth/success", language);
		break;
		default: return "undefined atatus";
	}
}//END: userAuthToString




/*
 * Аутентификация пользователя на основании заданного логина и пароля
 */
auth_state_e
userLogin(session_s * session, const char * login, const char * password){

	if(!session || !login || !password) RETURN_ERROR(AUTH_ERROR, "!session || !login || !password");
	uint32_t password_len = (uint32_t)strlen(password);
	uint32_t login_len = (uint32_t)strlen(login);
	if(!password_len || !login_len) return AUTH_LP_EMPTY;

	snode_s * snode = streeGet(_ulist.login, login);
	if(!snode || !snode->data) return AUTH_USER_EXISTS;
	user_s * user = (user_s *)snode->data;

	char hash[USER_PASSWORD_HASH_LEN + 1];
	hashSHA256(hash, password, password_len);

	if(!stringCompare(user->password, password) && !stringCompare(user->password, hash)) return AUTH_LP_INCORRECT;
	//printf("user->status = %u\n",user->status);
	if(user->status != 1) return AUTH_USER_LOCKED;

	session->user_id = user->user_id;

	return AUTH_OK;
}//END: userLogin






/***********************************************************************
 * Проверка соответствия данных пользователя заданным политикам
 **********************************************************************/

/*
 * Возвращает текстовое описание ошибки при работе с учетной записью на указанном языке
 * e - ошибка
 * language - требуемый язык сообщения (или NULL для исползования заданного по-умолчанию)
 */
const char * 
userErrorToString(ruser_e e, const char * language){
	language = langSelect(language, 0);
	switch(e){

		case RUSER_OK: return lang("/users/errors/none", language); break;					//OK
		case RUSER_ERROR: return lang("/users/errors/other", language); break;					//Внутренняя ошибка
		case RUSER_NOT_FOUND: return lang("/users/errors/notfound", language); break;					//Пользователь не найден

		case RUSER_LOGIN_EMPTY: return lang("/users/errors/login/empty", language); break;				//Не задан логин
		case RUSER_LOGIN_INCORRECT: return lang("/users/errors/login/incorrect", language); break;			//Логин задан некорректно
		case RUSER_LOGIN_TOO_SHORT: return lang("/users/errors/login/short", language); break;		//Логин слишком короткий
		case RUSER_LOGIN_TOO_LONG: return lang("/users/errors/login/long", language); break;		//Логин слишком длинный
		case RUSER_LOGIN_BUSY: return lang("/users/errors/login/busy", language); break;		//Логин уже занят

		case RUSER_PASSWORD_EMPTY: return lang("/users/errors/password/empty", language); break;		//Не задан пароль
		case RUSER_PASSWORD_INCORRECT: return lang("/users/errors/password/incorrect", language); break;	//Пароль задан некорректно
		case RUSER_PASSWORD_TOO_SHORT: return lang("/users/errors/password/short", language); break;	//Пароль слишком короткий
		case RUSER_PASSWORD_TOO_LONG: return lang("/users/errors/password/long", language); break;	//Пароль слишком длинный

		case RUSER_USERNAME_EMPTY: return lang("/users/errors/username/empty", language); break;		//Не задано имя пользователя
		case RUSER_USERNAME_INCORRECT: return lang("/users/errors/username/incorrect", language); break;	//Имя пользователя задан некорректно
		case RUSER_USERNAME_TOO_LONG: return lang("/users/errors/username/long", language); break;	//Пароль слишком длинный

		case RUSER_EMAIL_EMPTY: return lang("/users/errors/email/empty", language); break;				//Не задан адрес электронной почты
		case RUSER_EMAIL_INCORRECT: return lang("/users/errors/email/incorrect", language); break;			//Email задан некорректно
		case RUSER_EMAIL_TOO_LONG: return lang("/users/errors/email/long", language); break;	//Пароль слишком короткий

		case RUSER_PHONE_EMPTY: return lang("/users/errors/phone/empty", language); break;				//Не задан номер контактного телефона
		case RUSER_PHONE_INCORRECT: return lang("/users/errors/incorrect/phone", language); break;			//Номер телефона задан некорректно

		default: return "undefined error";
	}
}//END: userErrorToString



/*
 * Проверка логина на соответствие заданным политикам
 */
ruser_e
userPolicyCheckLogin(const char * login){
	if(!login || !login[0]) return RUSER_LOGIN_EMPTY;
	const u_char * ptr = (const u_char *)login;
	uint32_t n = 0;
	while(*ptr){
		if(_char_index[*ptr] == -1) return RUSER_LOGIN_INCORRECT;
		ptr++;
		n++;
	}
	if(n < USER_LOGIN_MIN) return RUSER_LOGIN_TOO_SHORT;
	if(n > USER_LOGIN_LEN) return RUSER_LOGIN_TOO_LONG;
	return RUSER_OK;
}//END: userPolicyCheckLogin



/*
 * Проверка пароля на соответствие заданным политикам
 */
ruser_e
userPolicyCheckPassword(const char * password){
	if(!password || !password[0]) return RUSER_PASSWORD_EMPTY;
	const u_char * ptr = (const u_char *)password;
	uint32_t n = 0;
	while(*ptr){
		if(_print_index[*ptr] == -1) return RUSER_PASSWORD_INCORRECT;
		ptr++;
		n++;
	}
	if(n < USER_PASSWORD_MIN) return RUSER_PASSWORD_TOO_SHORT;
	if(n > USER_PASSWORD_LEN) return RUSER_PASSWORD_TOO_LONG;
	return RUSER_OK;
}//END: userPolicyCheckPassword



/*
 * Проверка имени пользователя
 */
ruser_e
userPolicyCheckUsername(const char * username){
	if(!username || !username[0]) return RUSER_USERNAME_EMPTY;
	uint32_t n = 0;
	const u_char * ptr = (const u_char *)username;
	while(*ptr){
		if(*ptr < 32) return RUSER_USERNAME_INCORRECT;
		ptr++;
		n++;
	}
	if(n > USER_USERNAME_LEN) return RUSER_USERNAME_TOO_LONG;
	return RUSER_OK;
}//END: userPolicyCheckUsername




/*
 * Проверка адреса электронной почты
 */
ruser_e
userPolicyCheckEmail(const char * email){
	if(!email || !email[0]) return RUSER_EMAIL_EMPTY;
	uint32_t n = strlen(email);
	if(n > USER_EMAIL_LEN) return RUSER_EMAIL_TOO_LONG;
	if(!isValidEmail(email)) return RUSER_EMAIL_INCORRECT;
	return RUSER_OK;
}//END: userPolicyCheckEmail






/***********************************************************************
 * Работа с учетной записью пользователя
 **********************************************************************/

/*
 * Создание новой учетной записи пользователя
 * data - объект KV, содержащий поля типа ключ->значение, где ключем выступает имя поля таблицы пользователей
 */
user_s *
userCreate(kv_s * data, ruser_e * e){
	#define _ret(u,r) do{if(e)*e=r;return u;}while(0)
	if(!data || data->type != KV_OBJECT) _ret(NULL, RUSER_ERROR);
	ruser_e r;
	kv_s * login_kv		= kvSearch(data, CONST_STR_COMMA_LEN(USER_TABLE_LOGIN));
	kv_s * password_kv	= kvSearch(data, CONST_STR_COMMA_LEN(USER_TABLE_PASSWORD));
	kv_s * username_kv	= kvSearch(data, CONST_STR_COMMA_LEN(USER_TABLE_USERNAME));
	kv_s * email_kv		= kvSearch(data, CONST_STR_COMMA_LEN(USER_TABLE_EMAIL));

	//Обязательные поля
	if(!login_kv || login_kv->type != KV_STRING) _ret(NULL, RUSER_LOGIN_EMPTY); 
	if(!password_kv || password_kv->type != KV_STRING) _ret(NULL, RUSER_PASSWORD_EMPTY);
	if(!username_kv || username_kv->type != KV_STRING) _ret(NULL, RUSER_USERNAME_EMPTY);
	if(!email_kv || email_kv->type != KV_STRING) _ret(NULL, RUSER_EMAIL_EMPTY);
	if((r = userPolicyCheckLogin(login_kv->value.v_string.ptr)) != RUSER_OK) _ret(NULL, r);
	if((r = userPolicyCheckPassword(password_kv->value.v_string.ptr)) != RUSER_OK) _ret(NULL, r);
	if((r = userPolicyCheckUsername(username_kv->value.v_string.ptr)) != RUSER_OK) _ret(NULL, r);
	if((r = userPolicyCheckEmail(email_kv->value.v_string.ptr)) != RUSER_OK) _ret(NULL, r);

	mysql_s * db = mysqlGetInstance(XG_MYSQL_INSTANCES, DB_INSTANCE_MAIN);
	if(!db) _ret(NULL, RUSER_ERROR);

	//Проверка занятости логина
	snode_s * snode = streeGet(_ulist.login, login_kv->value.v_string.ptr);
	if(snode && snode->data) _ret(NULL, RUSER_LOGIN_BUSY);

	kv_s * kv 			= kvFill(kvCopy(NULL, db_table_user), data);
	kv_s * user_id_kv	= kvSearch(kv, CONST_STR_COMMA_LEN(USER_TABLE_USER_ID));
	if(user_id_kv) kvSetNull(user_id_kv);

	//Получение SHA256 хеша пароля
	char hash[USER_PASSWORD_HASH_LEN + 1];
	hashSHA256(hash, password_kv->value.v_string.ptr, password_kv->value.v_string.len);
	kvAppendString(kv, USER_TABLE_PASSWORD, hash, USER_PASSWORD_HASH_LEN, KV_REPLACE);

/*
	buffer_s * buf = bufferCreate(0);
	kvEcho(kv, KVF_HEADERS, buf);
	bufferPrint(buf);
	bufferFree(buf);
*/
	//Добавление записи в базу банных

	if(!mysqlInsert(db, USER_TABLE_NAME, kv, NULL)){
		kvFree(kv);
		_ret(NULL, RUSER_ERROR);
	}
	kvSetInt(user_id_kv, db->last_insert_id);
	mysqlFreeResult(db);
	//printf("new user id = %u\n",(uint32_t)user_id);

	user_s * user = userStructGet();
	user->info = kv;
	_fillUserStruct(user, user->info);
	_configUserAttr(user);

	//Добавление в кеш нового пользователя
	pthread_mutex_lock(&user_mutex);
		darraySetId(_ulist.list, user->user_id, user);
		streeSet(_ulist.login, user->login, user, SNODE_REPLACE);
	pthread_mutex_unlock(&user_mutex);

	_ret(user, RUSER_OK);
	#undef _ret
}//END: userCreate



/*
 * Обновление учетной записи пользователя
 * data - объект KV, содержащий поля типа ключ->значение, где ключем выступает имя поля таблицы пользователей
 */
ruser_e
userUpdate(uint32_t user_id, kv_s * data){
	#define _ret(u) do{kvFree(kv);kvFree(where_kv);return u;}while(0)
	if(!data) return RUSER_ERROR;
	user_s * user = (user_s *)darrayGetPointer(_ulist.list, user_id, NULL);
	if(!user) return RUSER_NOT_FOUND;
	mysql_s * db = mysqlGetInstance(XG_MYSQL_INSTANCES, DB_INSTANCE_MAIN);
	if(!db) return RUSER_ERROR;
	ruser_e r;
	kv_s * kv = kvIntersect(data, db_table_user);
	kv_s * where_kv = NULL;
	kv_s * user_id_kv	= kvSearch(kv, CONST_STR_COMMA_LEN(USER_TABLE_USER_ID));
	if(user_id_kv){
		kvFree(user_id_kv);
	}

	if(!kv->value.v_list.first)_ret(RUSER_OK);

	kv_s * username_kv	= kvSearch(kv, CONST_STR_COMMA_LEN(USER_TABLE_USERNAME));
	if(username_kv){
		if(username_kv->type != KV_STRING) _ret(RUSER_USERNAME_EMPTY);
		if((r = userPolicyCheckUsername(username_kv->value.v_string.ptr)) != RUSER_OK) _ret(r);
	}

	kv_s * email_kv		= kvSearch(kv, CONST_STR_COMMA_LEN(USER_TABLE_EMAIL));
	if(email_kv){
		if(email_kv->type != KV_STRING) _ret(RUSER_EMAIL_EMPTY);
		if((r = userPolicyCheckEmail(email_kv->value.v_string.ptr)) != RUSER_OK) _ret(r);
	}

	kv_s * login_kv		= kvSearch(kv, CONST_STR_COMMA_LEN(USER_TABLE_LOGIN));
	if(login_kv){
		if(login_kv->type != KV_STRING) _ret(RUSER_LOGIN_EMPTY);
		if((r = userPolicyCheckLogin(login_kv->value.v_string.ptr)) != RUSER_OK) _ret(r);
		if(stringCompareCase(user->login, login_kv->value.v_string.ptr)){
			kvFree(login_kv);
			login_kv = NULL;
		}else{
			snode_s * snode = streeGet(_ulist.login, login_kv->value.v_string.ptr);
			if(snode && snode->data) _ret(RUSER_LOGIN_BUSY);
		}
	}

	kv_s * password_kv	= kvSearch(kv, CONST_STR_COMMA_LEN(USER_TABLE_PASSWORD));
	if(password_kv){
		if(password_kv->type != KV_STRING) _ret(RUSER_PASSWORD_EMPTY);
		if((r = userPolicyCheckPassword(password_kv->value.v_string.ptr)) != RUSER_OK) _ret(r);
		char hash[USER_PASSWORD_HASH_LEN + 1];
		hashSHA256(hash, password_kv->value.v_string.ptr, password_kv->value.v_string.len);
		kvSetString(password_kv, hash, USER_PASSWORD_HASH_LEN);
	}

	where_kv = kvNewRoot();
	kvAppendInt(where_kv, USER_TABLE_USER_ID, (int64_t)user_id, KV_INSERT);

	if(!mysqlUpdate(db, USER_TABLE_NAME, where_kv, kv, NULL)) _ret(RUSER_ERROR);

	//Обновление в кеше существующего пользователя
	pthread_mutex_lock(&user_mutex);
		kvFill(user->info, kv);
		if(login_kv){
			streeDelete(_ulist.login, user->login);
			streeSet(_ulist.login, login_kv->value.v_string.ptr, user, SNODE_REPLACE);
		}
		_fillUserStruct(user, user->info);
		_configUserAttr(user);
	pthread_mutex_unlock(&user_mutex);

	_ret(RUSER_OK);
	#undef _ret
}//END: userUpdate

















