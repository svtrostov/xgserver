/***********************************************************************
 * XG SERVER
 * framework/user.h
 * Работа с пользователями: аутентификация, добавление, удаление, получение сведений
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   



#ifndef _XG_FRAMEWORK_USER_H
#define _XG_FRAMEWORK_USER_H

#ifdef __cplusplus
extern "C" {
#endif


/***********************************************************************
 * Подключаемые заголовки
 **********************************************************************/

#include <openssl/sha.h>
#include "core.h"
#include "kv.h"
#include "server.h"
#include "session.h"
#include "darray.h"
#include "stree.h"


/***********************************************************************
 * Константы
 **********************************************************************/






/***********************************************************************
 * Объявления и декларации
 **********************************************************************/

//Длинна строки, занимаемая user_id
#define USER_ID_LEN 11

//Длинна строки, занимаемая login (максимум)
#define USER_LOGIN_LEN 32
#define USER_LOGIN_MIN 5


//Длинна строки, занимаемая password (максимум)
#define USER_PASSWORD_LEN 32
#define USER_PASSWORD_MIN 8

//Длинна строки, занимаемая хешем пароля (SHA-256 -> 64 байта)
#define USER_PASSWORD_HASH_LEN 64 

//Длинна строки, занимаемая language (максимум)
#define USER_LANGUAGE_LEN 16

//Длинна строки, занимаемая email (максимум)
#define USER_EMAIL_LEN 128

//Длинна строки, занимаемая username (максимум)
#define USER_USERNAME_LEN 128


//Названия в session_s
//#define USER_SESSION_NAME		"user"			//Название записи в сессии
//#define ACCOUNT_SESSION_NAME	"acct"			//Название записи в сессии

//Названия в базе данных
#define USER_TABLE_NAME			"users"			//Название таблицы в базе данных
#define USER_TABLE_USER_ID		"user_id"		//Идентификатор пользователя
#define USER_TABLE_ACCOUNT_ID	"account_id"	//Идентификатор счета пользователя
#define USER_TABLE_STATUS		"status"		//Статус учетной записи пользователя (0 - заблокирован)
#define USER_TABLE_DELETED		"is_deleted"	//Признак, указывающий что учетная запись удалена
#define USER_TABLE_AL			"access_level"	//Уровень доступа пользователя (0 - обычный пользователь, 1 — администратор)
#define USER_TABLE_LOGIN		"login"			//Логин
#define USER_TABLE_PASSWORD		"password"		//Пароль
#define USER_TABLE_LANGUAGE		"language"		//Двузначный код языка пользователя: EN, RU, DE, FR и т.д.
#define USER_TABLE_USERNAME		"name"			//Полное имя пользователя
#define USER_TABLE_EMAIL		"email"			//Контактный email
#define USER_TABLE_PHONE		"phone"			//Контактный телефон
#define USER_TABLE_CREATE_TS	"create_ts"		//Дата и время создания записи
#define USER_TABLE_UPDATE_TS	"update_ts"		//Дата и время изменения записи
#define USER_TABLE_CREATE_USER	"create_user"	//Пользователь, создавший запись
#define USER_TABLE_UPDATE_USER	"update_user"	//Пользователь, изменивший запись



#define ACCOUNT_TABLE_NAME		"accounts"		//Название таблицы в базе данных
#define ACCOUNT_TABLE_ID		"account_id"	//Идентификатор счета
#define ACCOUNT_TABLE_USER_ID	"user_id"		//Идентификатор пользователя
#define ACCOUNT_TABLE_STATUS	"status"		//Статус учетной записи счета (0 - заблокирован)

#define UACCESS_TABLE_NAME		"uaccess"		//Права доступа пользователей
#define UACCESS_TABLE_USER_ID	"user_id"		//Идентификатор пользователя
#define UACCESS_TABLE_RIGHT_ID	"right_id"		//Идентификатор права доступа



//Статус пользователя при аутентификации
typedef enum{
	AUTH_NONE			= 0,	//Пользователь не аутентифицирован
	AUTH_ERROR			= 1,	//Ошибка аутентификации (какая-то внутренняя проблема: нет связи с СУБД и т.д.) - ошибка сервера 500
	AUTH_LP_EMPTY		= 2,	//Логин или пароль не заданы
	AUTH_LP_INCORRECT	= 3,	//Неверно указаны логин или пароль
	AUTH_USER_EXISTS	= 4,	//Пользователь с указанным логином не существует
	AUTH_USER_LOCKED	= 5,	//Учетная запись пользователя заблокирована
	AUTH_USER_UNTIED	= 6,	//Учетная запись пользователя не привязана к какому-либо счету
	AUTH_ACCOUNT_LOCKED	= 7,	//Счет заблокирован
	AUTH_DENIED			= 8,	//Доступ к учетной записи запрещен при текущих условиях входа, при этом учетная запись не заблокирована (например, по IP адресу)
	AUTH_EXPIRED		= 9,	//Время сессии истекло
	AUTH_OK				= 10	//Все ок
} auth_state_e;


//Результаты обработки действий с учетной записью пользователя
typedef enum{

	RUSER_OK			= 0,	//Все ок
	RUSER_ERROR,				//Ошибка
	RUSER_NOT_FOUND,			//Пользователь не найден

	RUSER_LOGIN_EMPTY,			//Не задан логин
	RUSER_LOGIN_INCORRECT,		//Логин задан некорректно
	RUSER_LOGIN_TOO_SHORT,		//Логин слишком короткий
	RUSER_LOGIN_TOO_LONG,		//Логин слишком длинный
	RUSER_LOGIN_BUSY,			//Логин занят (уже существует)

	RUSER_PASSWORD_EMPTY,		//Не задан пароль
	RUSER_PASSWORD_INCORRECT,	//Пароль задан некорректно
	RUSER_PASSWORD_TOO_SHORT,	//Пароль слишком короткий
	RUSER_PASSWORD_TOO_LONG,	//Пароль слишком длинный

	RUSER_USERNAME_EMPTY,		//Не задано имя пользователя
	RUSER_USERNAME_INCORRECT,	//Имя пользователя задан некорректно
	RUSER_USERNAME_TOO_LONG,	//Имя пользователя слишком длинное

	RUSER_EMAIL_EMPTY,			//Не задан адрес электронной почты
	RUSER_EMAIL_INCORRECT,		//Email задан некорректно
	RUSER_EMAIL_TOO_LONG,		//Адрес электронной почты слишком длинный

	RUSER_PHONE_EMPTY,			//Не задан номер контактного телефона
	RUSER_PHONE_INCORRECT,		//Номер телефона задан некорректно

} ruser_e;



/***********************************************************************
 * Структуры
 **********************************************************************/

typedef		struct type_user_s		user_s;

/*
CREATE TABLE IF NOT EXISTS `users` (
  `user_id` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `account_id` int(10) unsigned NOT NULL,
  `status` int(10) unsigned NOT NULL,
  `access_level` int(10) unsigned NOT NULL,
  `login` char(32) NOT NULL,
  `password` char(64) NOT NULL,
  `language` char(16) NOT NULL,
  `is_deleted` int(1) unsigned NOT NULL,
  `name` char(255) NOT NULL,
  `email` char(128) NOT NULL,
  `create_ts` datetime NOT NULL,
  `update_ts` datetime NOT NULL,
  `create_user` int(10) unsigned NOT NULL,
  `update_user` int(10) unsigned NOT NULL,
  PRIMARY KEY (`user_id`),
  UNIQUE KEY `login` (`login`)
) ENGINE=InnoDB  DEFAULT CHARSET=utf8 COMMENT='Users' AUTO_INCREMENT=2 ;

INSERT INTO `users` (`user_id`, `account_id`, `status`, `access_level`, `login`, `password`, `language`, `is_deleted`, `name`, `email`, `create_ts`, `update_ts`, `create_user`, `update_user`) VALUES(1, 0, 1, 1, 'admin', 'admin', 'en', 0, '', '', '0000-00-00 00:00:00', '0000-00-00 00:00:00', 0, 0);
*/


//Структура информации о пользователе
typedef struct type_user_s{
	uint32_t		user_id;								//Идентификатор пользователя
	uint32_t		account_id;								//Идентификатор аккаунта пользователя
	uint32_t		access_level;							//Уровень доступа пользователя (0 - обычный пользователь, 1 — администратор c какими-то привелегиями)
	uint32_t		status;									//Текущий статус учетной записи пользователя: 0 - удален, 1 - активен, 2 - заблокирован
	char			login[USER_LOGIN_LEN + 1];				//Логин
	uint32_t		login_n;								//Логин - длинна строки 
	char			password[USER_PASSWORD_HASH_LEN + 1];	//Пароль
	uint32_t		password_n;								//Пароль - длинна строки 
	char			language[USER_LANGUAGE_LEN + 1];		//Код языка пользователя: EN, RU, DE, FR, ENG, RUS, RU1, FENYA и т.д.
	uint32_t		language_n;								//Код языка пользователя - длинна строки 
	auth_state_e	auth_state;								//Статус аутентификации пользователя
	bool			is_super;								//Признак, указывающий что данный пользователь имеет все привелегии доступа
	kv_s			* info;									//Указатель на структуру KV с данными пользователя
	user_s			* next;									//Следующая запись
} user_s;


//Структура списка пользователей
typedef struct type_userlist_s{
	stree_s		* login;		//Бинарное дерево поиска пользователей по логину
	darray_s	* list;			//Список пользователей
	uint32_t	min_id;			//Минимальный ID в списке
	uint32_t	max_id;			//Максимальный ID в списке
	uint32_t	count;			//Количество записей в списке
} userlist_s;





/***********************************************************************
 * Функции
 **********************************************************************/

inline user_s * userStructGet(void);	//Получение структуры пользователя
inline void		userStructFree(void * ptr);	//Освобождение структуры пользователя

result_e		userlistLoadFromDB(void);	//Загрузка списка пользователей из базы данных

user_s * 		userFromId(uint32_t user_id);	//Получение структуры данных пользователя из ID пользователя
user_s * 		userFromLogin(const char * login);	//Получение структуры данных пользователя из логина
user_s * 		userFromSession(session_s * session);	//Получение структуры данных пользователя из сессии
bool			userToSession(user_s * user);	//Запись структуры данных пользователя в сессию

const char * 	userAuthToString(auth_state_e auth, const char * language);	//Возвращает текстовое описание статуса аутентификации на указанном языке
auth_state_e	userLogin(session_s * session, const char * login, const char * password);	//Получение структуры данных пользователя из базы данных при аутентификации пользователя

const char * 	userErrorToString(ruser_e e, const char * language);	//Возвращает текстовое описание ошибки при работе с учетной записью на указанном языке
ruser_e			userPolicyCheckLogin(const char * login);	//Проверка логина на соответствие заданным политикам
ruser_e			userPolicyCheckPassword(const char * password);	//Проверка пароля на соответствие заданным политикам
ruser_e			userPolicyCheckUsername(const char * username);	//Проверка имени пользователя
ruser_e			userPolicyCheckEmail(const char * email);	//Проверка адреса электронной почты

user_s *		userCreate(kv_s * data, ruser_e * e);	//Создание новой учетной записи пользователя


#ifdef __cplusplus
}
#endif

#endif //_XG_FRAMEWORK_USER_H
