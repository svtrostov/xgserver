/***********************************************************************
 * XG SERVER
 * core/ajax.c
 * Работа с AJAX ответом в JSON формате
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include "core.h"
#include "globals.h"
#include "server.h"
#include "kv.h"


/*
 * Структура AJAX ответа в JSON формате:
 * 
 * 
 * {
 *	"title: "", 	//Заголовок страницы 
 *	"location: "",	//Провести редирект на указанный URL методом GET
 * 
 *	//Выполнить запрос по AJAX на указанный URL методом POST
 *	"post":{
 *		"location"	: "",										//URL для выполнения POST запроса
 *		"data"		: {[key]:[value], ..., [key]:[value]},		//Параметры, передаваемые в POST запросе
 *		"callback"	: ""										//Функция JavaScript, которая должна быть вызвана по завершении запроса
 *	},
 *	
 *	//Подключить на страницу медиа-файл (JS, CSS  и т.п.)
 *	"required":[
 *		{
 *			"url"		: "",	//путь и имя файла относительно корневой директории
 *			"call"		: ""	//Функция, которая должна быть вызвана из подключенного скрипта после его загрузки
 *		}
 *	],
 *	
 *	//Сообщения, отправляемые клиенту
 *	"messages":[
 *		{
 *			"id"		: "",	//Идентификатор сообщения, если установлено, то у пользователя будет возможность игнорировать сообщение в дальнейшем (например, записью ID сообщения в cookie)
 *			"title"		: "",	//Заголовок сообщения
 *			"text"		: "",	//Текст сообщения
 *			"type"		: "",	//Тип сообщения: success|error|warning|info
 *			"display"	: ""	//Тип отображения клиенту на экране: none|window|hint
 *		}
 *	],
 *	
 *	"data": { ... },	//Произвольные данные для клиента - результаты обработки запроса
 *	
 *	"callback": "",		//Функция JavaScript, которая должна быть вызвана по завершении запроса
 *	
 *	"status": "",		//Статус обработки запроса none|success|error|relogin
 *	
 *	"action": "",		//Действие, которое было запрошено со стороны клиента
 *	
 *	"document": "",		//URI запрошенного документа (/main/login, например)
 *	
 *	//HTML контент, возвращаемый через AJAX запрос
 *	"content": [
 *		{
 *			"parent": "",	//ID тега (для document.getElementById(parent)), в который требуется вставить контент
 *			"content": "",	//HTML контент
 *			"append": ""	//Тип добавления контента: set|begin|end|before|after
 *		}
 * ],
 *	
 *	"timestamp": 12345,	//Числовой штамп времени от начала UNIX эпохи
 *	
 *	"ruid": "",			//Уникальный ID запроса, полученный от клиента
 *	
 *	//Дополнительный стек данных для обработки на клиенте
 *	//в отличии от data, являющегося результатом обработки запроса и массивом данных ответа,
 *	//стек хранит в себе общие данные приложения и обработка происходит вне зависимости от статуса обработки основного запроса
 *	//Например, какие-то системные сообщения, уведомления и т.д.
 *	"stack": { ... }
 *	}
 */ 





/***********************************************************************
 * Функции
 **********************************************************************/


/*
 * Создание структуры AJAX ответа сервера для соединения
 */
ajax_s *
ajaxNew(connection_s * con){
	uint32_t len;
	ajax_s * ajax = (ajax_s *)mNewZ(sizeof(ajax_s));
	ajax->connection = con;
	ajax->root = kvNewRoot();
	kvAppendInt(ajax->root,		"timestamp",	(int64_t)time(NULL), KV_INSERT);									//Штамп времени

	kvSetString(kvAppend(ajax->root, CONST_STR_COMMA_LEN("document"), KV_INSERT), con->request.uri.path.ptr, con->request.uri.path.len);	//URI запрошенного документа
	kvSetString(kvAppend(ajax->root, CONST_STR_COMMA_LEN("ruid"), KV_INSERT), requestGetGPC(con, "ruid", "pg", &len), len);					//Уникальный ID запроса
	ajax->action_kv = kvSetString(kvAppend(ajax->root, CONST_STR_COMMA_LEN("action"), KV_INSERT), requestGetGPC(con, "action", "pg", &len), len);	//Название действия
	ajax->action		= (const char *)ajax->action_kv->value.v_string.ptr;
	return ajax;
}//END: ajaxNew



/*
 * Освобождение структуры AJAX 
 */
void
ajaxFree(ajax_s * ajax){
	if(!ajax || !ajax->root) return;
	if(ajax->root) kvFree(ajax->root);
	mFree(ajax);
	return;
}//END: ajaxFree



/*
 * Устанавливает заголовок документа
 */
void
ajaxSetTitle(ajax_s * ajax, const char * title, uint32_t title_len){
	if(!ajax || !ajax->root) return;
	if(!ajax->title_kv) ajax->title_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("title"), KV_INSERT), KV_STRING);
	kvSetString(ajax->title_kv, title, title_len); 
}//END: ajaxSetTitle



/*
 * Устанавливает URL редиректа
 */
void
ajaxSetLocation(ajax_s * ajax, const char * url, uint32_t url_len){
	if(!ajax || !ajax->root) return;
	if(!ajax->location_kv) ajax->location_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("location"), KV_INSERT), KV_STRING);
	kvSetString(ajax->location_kv, url, url_len); 
}//END: ajaxSetLocation



/*
 * Устанавливает функцию JavaScript, которая должна быть вызвана по завершении запроса
 */
void
ajaxSetCallback(ajax_s * ajax, const char * call, uint32_t call_len){
	if(!ajax || !ajax->root) return;
	if(!ajax->callback_kv) ajax->callback_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("callback"), KV_INSERT), KV_STRING);
	kvSetString(ajax->location_kv, call, call_len); 
}//END: ajaxSetCallback



/*
 * Устанавливает cтатус обработки запроса
 */
void
ajaxSetStatus(ajax_s * ajax, ajax_status_e status){
	if(!ajax || !ajax->root) return;
	if(!ajax->status_kv) ajax->status_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("status"), KV_INSERT), KV_STRING);
	switch(status){
		case AJAX_STATUS_NONE:		kvSetString(ajax->status_kv, CONST_STR_COMMA_LEN("none"));		break;		//Запрос не обработан
		case AJAX_STATUS_SUCCESS:	kvSetString(ajax->status_kv, CONST_STR_COMMA_LEN("success"));	break;		//Запрос обработан успешно
		case AJAX_STATUS_ERROR:		kvSetString(ajax->status_kv, CONST_STR_COMMA_LEN("error"));		break;		//Запрос обработан с ошибками
		case AJAX_STATUS_RELOGIN:	kvSetString(ajax->status_kv, CONST_STR_COMMA_LEN("relogin"));	break;		//Требуется повторная аутентификация клиента
	}
}//END: ajaxSetStatus



/*
 * Добавление в массив подключаемых для страницы медиа-файлов нового элемента
 */
void
ajaxAddRequired(ajax_s * ajax, const char * url, uint32_t url_len, const char * call, uint32_t call_len){
	if(!ajax || !ajax->root || !url) return;
	if(!ajax->required_kv) ajax->required_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("required"), KV_INSERT), KV_ARRAY);
	kv_s * req = kvAppendObject(ajax->required_kv, NULL, KV_INSERT);
	kvSetString(kvAppend(req, CONST_STR_COMMA_LEN("url"), KV_INSERT), url, url_len);
	kvSetString(kvAppend(req, CONST_STR_COMMA_LEN("call"), KV_INSERT), call, call_len); 
}//END: ajaxAddRequired



/*
 * Очистка массива подключаемых для страницы медиа-файлов
 */
void
ajaxRequiredClear(ajax_s * ajax){
	if(!ajax || !ajax->required_kv) return;
	kvClear(ajax->required_kv);
}//END: ajaxRequiredClear



/*
 * Добавление HTML контента, возвращаемого через AJAX запрос
 * parent - название HTML родительского элемента
 * content - HTML контент
 * append - тип вставки контента в элемент
 */
void
ajaxAddContent(ajax_s * ajax, const char * parent, uint32_t parent_len, const char * content, uint32_t content_len, content_append_e append){
	if(!ajax || !ajax->root) return;
	if(!ajax->content_kv) ajax->content_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("content"), KV_INSERT), KV_ARRAY);
	kv_s * content_kv = kvAppendObject(ajax->content_kv, NULL, KV_INSERT);
	kvSetString(kvAppend(content_kv, CONST_STR_COMMA_LEN("parent"), KV_INSERT), parent, parent_len);
	kvSetString(kvAppend(content_kv, CONST_STR_COMMA_LEN("content"), KV_INSERT), content, content_len);
	kv_s * append_kv = kvAppend(content_kv, CONST_STR_COMMA_LEN("append"), KV_INSERT); 
	switch(append){
		case CONTENT_APPEND_SET:	kvSetString(append_kv, CONST_STR_COMMA_LEN("set"));		break;	//заменить содержимое родительского элемента заданным контентом
		case CONTENT_APPEND_BEGIN:	kvSetString(append_kv, CONST_STR_COMMA_LEN("begin"));	break;	//добавить в начало родительского элемента
		case CONTENT_APPEND_END:	kvSetString(append_kv, CONST_STR_COMMA_LEN("end"));		break;	//добавить в конец родительского элемента
		case CONTENT_APPEND_BEFORE:	kvSetString(append_kv, CONST_STR_COMMA_LEN("before"));	break;	//добавить перед родительским элементом
		case CONTENT_APPEND_AFTER:	kvSetString(append_kv, CONST_STR_COMMA_LEN("after"));	break;	//добавить после родительского элемента
	}
}//END: ajaxAddContent



/*
 * Очистка массива контента
 */
void
ajaxContentClear(ajax_s * ajax){
	if(!ajax || !ajax->root || !ajax->content_kv) return;
	kvClear(ajax->content_kv);
}//END: ajaxContentClear



/*
 * Добавление сообщения в массив сообщений, отправляемых клиенту от сервера, возникших в процессе обработки запроса
 * id - Идентификатор сообщения, если установлено, то у пользователя будет возможность игнорировать сообщение в дальнейшем (например, записью ID сообщения в cookie)
 * title - Заголовок сообщения
 * text - Текст сообщения
 * type - Тип сообщения: success|error|warning|info
 * display - Тип отображения клиенту на экране: none|window|hint
 */
void
ajaxAddMessage(
	ajax_s * ajax, 
	const char * id, uint32_t id_len, 
	const char * title, uint32_t title_len, 
	const char * text, uint32_t text_len,
	message_type_e type,
	message_display_e display
){
	if(!ajax || !ajax->root) return;
	if(!ajax->messages_kv) ajax->messages_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("messages"), KV_INSERT), KV_ARRAY);
	kv_s * message = kvAppendObject(ajax->content_kv, NULL, KV_INSERT);
	kvSetString(kvAppend(message, CONST_STR_COMMA_LEN("id"), KV_INSERT), id, id_len);
	kvSetString(kvAppend(message, CONST_STR_COMMA_LEN("title"), KV_INSERT), title, title_len);
	kvSetString(kvAppend(message, CONST_STR_COMMA_LEN("text"), KV_INSERT), text, text_len);
	kv_s * type_kv = kvAppend(message, CONST_STR_COMMA_LEN("type"), KV_INSERT); 
	switch(type){
		case AJAX_MESSAGE_INFO:		kvSetString(type_kv, CONST_STR_COMMA_LEN("info"));		break;	//Информационное сообщение
		case AJAX_MESSAGE_WARNING:	kvSetString(type_kv, CONST_STR_COMMA_LEN("warning"));	break;	//Предупреждение
		case AJAX_MESSAGE_ERROR:	kvSetString(type_kv, CONST_STR_COMMA_LEN("error"));		break;	//Ошибка
		case AJAX_MESSAGE_SUCCESS:	kvSetString(type_kv, CONST_STR_COMMA_LEN("success"));	break;	//Сообщение об успешном выполнении
	}
	kv_s * display_kv = kvAppend(message, CONST_STR_COMMA_LEN("display"), KV_INSERT); 
	switch(display){
		case AJAX_MESSAGE_HIDE:		kvSetString(display_kv, CONST_STR_COMMA_LEN("hide"));	break;	//Не отображать сообщение
		case AJAX_MESSAGE_WINDOW:	kvSetString(display_kv, CONST_STR_COMMA_LEN("window"));	break;	//Обобразить сообщение в модальном окне
		case AJAX_MESSAGE_HINT:		kvSetString(display_kv, CONST_STR_COMMA_LEN("hind"));	break;	//Обобразить сообщение во всплывающей подсказке
		case AJAX_MESSAGE_CLIENT:	kvSetString(display_kv, CONST_STR_COMMA_LEN("client"));	break;	//Способ отображения сообщения выбирает клиентское приложение
	}
}//END: ajaxAddMessage



/*
 * Очистка массива сообщений
 */
void
ajaxMessagesClear(ajax_s * ajax){
	if(!ajax || !ajax->messages_kv) return;
	kvClear(ajax->messages_kv);
}//END: ajaxMessagesClear



/*
 * Добавление элемента в дополнительный стек данных для обработки на клиенте - KV
 */
void
ajaxSetStackKV(ajax_s * ajax, const char * name, uint32_t name_len, kv_s * kv){
	if(!ajax || !ajax->root || !name || !kv) return;
	if(!ajax->stack_kv) ajax->stack_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("stack"), KV_INSERT), KV_OBJECT);
	kv_s * stack_kv = kvClear(kvAppend(ajax->stack_kv, name, name_len, KV_REPLACE));
	kvInsert(stack_kv, kv, KV_INSERT);
}//END: ajaxSetStackKV



/*
 * Добавление элемента в дополнительный стек данных для обработки на клиенте - Bool
 */
void
ajaxSetStackBool(ajax_s * ajax, const char * name, uint32_t name_len, bool value){
	if(!ajax || !ajax->root || !name) return;
	if(!ajax->stack_kv) ajax->stack_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("stack"), KV_INSERT), KV_OBJECT);
	kvSetBool(kvAppend(ajax->stack_kv, name, name_len, KV_REPLACE), value);
}//END: ajaxSetStackBool



/*
 * Добавление элемента в дополнительный стек данных для обработки на клиенте - Int
 */
void
ajaxSetStackInt(ajax_s * ajax, const char * name, uint32_t name_len, int64_t value){
	if(!ajax || !ajax->root || !name) return;
	if(!ajax->stack_kv) ajax->stack_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("stack"), KV_INSERT), KV_OBJECT);
	kvSetInt(kvAppend(ajax->stack_kv, name, name_len, KV_REPLACE), value);
}//END: ajaxSetStackInt



/*
 * Добавление элемента в дополнительный стек данных для обработки на клиенте - Double
 */
void
ajaxSetStackDouble(ajax_s * ajax, const char * name, uint32_t name_len, double value){
	if(!ajax || !ajax->root || !name) return;
	if(!ajax->stack_kv) ajax->stack_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("stack"), KV_INSERT), KV_OBJECT);
	kvSetDouble(kvAppend(ajax->stack_kv, name, name_len, KV_REPLACE), value);
}//END: ajaxSetStackDouble



/*
 * Добавление элемента в дополнительный стек данных для обработки на клиенте - String
 */
void
ajaxSetStackString(ajax_s * ajax, const char * name, uint32_t name_len, const char * str, uint32_t str_len){
	if(!ajax || !name || !str) return;
	if(!ajax->stack_kv) ajax->stack_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("stack"), KV_INSERT), KV_OBJECT);
	kvSetString(kvAppend(ajax->stack_kv, name, name_len, KV_REPLACE), str, str_len);
}//END: ajaxSetStackString



/*
 * Добавление элемента в дополнительный стек данных для обработки на клиенте - String pointer
 */
void
ajaxSetStackStringPtr(ajax_s * ajax, const char * name, uint32_t name_len, char * str, uint32_t str_len){
	if(!ajax || !ajax->root || !name || !str) return;
	if(!ajax->stack_kv) ajax->stack_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("stack"), KV_INSERT), KV_OBJECT);
	kvSetStringPtr(kvAppend(ajax->stack_kv, name, name_len, KV_REPLACE), str, str_len);
}//END: ajaxSetStackStringPtr



/*
 * Добавление элемента в дополнительный стек данных для обработки на клиенте - Json
 */
void
ajaxSetStackJson(ajax_s * ajax, const char * name, uint32_t name_len, const char * str, uint32_t str_len){
	if(!ajax || !ajax->root || !name || !str) return;
	if(!ajax->stack_kv) ajax->stack_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("stack"), KV_INSERT), KV_OBJECT);
	kvSetJson(kvAppend(ajax->stack_kv, name, name_len, KV_REPLACE), str, str_len);
}//END: ajaxSetStackJson



/*
 * Добавление элемента в дополнительный стек данных для обработки на клиенте - Json pointer
 */
void
ajaxSetStackJsonPtr(ajax_s * ajax, const char * name, uint32_t name_len, char * str, uint32_t str_len){
	if(!ajax || !ajax->root || !name || !str) return;
	if(!ajax->stack_kv) ajax->stack_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("stack"), KV_INSERT), KV_OBJECT);
	kvSetJsonPtr(kvAppend(ajax->stack_kv, name, name_len, KV_REPLACE), str, str_len);
}//END: ajaxSetStackJsonPtr



/*
 * Очистка стека
 */
void
ajaxStackClear(ajax_s * ajax){
	if(!ajax || !ajax->stack_kv) return;
	kvClear(ajax->stack_kv);
}//END: ajaxStackClear



/*
 * Добавление элемента в данные для клиента - KV
 */
void
ajaxSetDataKV(ajax_s * ajax, const char * name, uint32_t name_len, kv_s * kv){
	if(!ajax || !ajax->root || !name || !kv) return;
	if(!ajax->data_kv) ajax->data_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("data"), KV_INSERT), KV_OBJECT);
	kv_s * data_kv = kvClear(kvAppend(ajax->data_kv, name, name_len, KV_REPLACE));
	kvInsert(data_kv, kv, KV_INSERT);
}//END: ajaxSetDataKV



/*
 * Добавление элемента в данные для клиента - Bool
 */
void
ajaxSetDataBool(ajax_s * ajax, const char * name, uint32_t name_len, bool value){
	if(!ajax || !ajax->root || !name) return;
	if(!ajax->data_kv) ajax->data_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("data"), KV_INSERT), KV_OBJECT);
	kvSetBool(kvAppend(ajax->data_kv, name, name_len, KV_REPLACE), value);
}//END: ajaxSetDataBool



/*
 * Добавление элемента в данные для клиента - Int
 */
void
ajaxSetDataInt(ajax_s * ajax, const char * name, uint32_t name_len, int64_t value){
	if(!ajax || !ajax->root || !name) return;
	if(!ajax->data_kv) ajax->data_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("data"), KV_INSERT), KV_OBJECT);
	kvSetInt(kvAppend(ajax->data_kv, name, name_len, KV_REPLACE), value);
}//END: ajaxSetDataInt



/*
 * Добавление элемента в данные для клиента - Double
 */
void
ajaxSetDataDouble(ajax_s * ajax, const char * name, uint32_t name_len, double value){
	if(!ajax || !ajax->root || !name) return;
	if(!ajax->data_kv) ajax->data_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("data"), KV_INSERT), KV_OBJECT);
	kvSetDouble(kvAppend(ajax->data_kv, name, name_len, KV_REPLACE), value);
}//END: ajaxSetDataDouble



/*
 * Добавление элемента в данные для клиента - String
 */
void
ajaxSetDataString(ajax_s * ajax, const char * name, uint32_t name_len, const char * str, uint32_t str_len){
	if(!ajax || !ajax->root || !name || !str) return;
	if(!ajax->data_kv) ajax->data_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("data"), KV_INSERT), KV_OBJECT);
	kvSetString(kvAppend(ajax->data_kv, name, name_len, KV_REPLACE), str, str_len);
}//END: ajaxSetDataString



/*
 * Добавление элемента в данные для клиента - String pointer
 */
void
ajaxSetDataStringPtr(ajax_s * ajax, const char * name, uint32_t name_len, char * str, uint32_t str_len){
	if(!ajax || !ajax->root || !name || !str) return;
	if(!ajax->data_kv) ajax->data_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("data"), KV_INSERT), KV_OBJECT);
	kvSetStringPtr(kvAppend(ajax->data_kv, name, name_len, KV_REPLACE), str, str_len);
}//END: ajaxSetDataStringPtr



/*
 * Добавление элемента в данные для клиента - Json
 */
void
ajaxSetDataJson(ajax_s * ajax, const char * name, uint32_t name_len, const char * str, uint32_t str_len){
	if(!ajax || !ajax->root || !name || !str) return;
	if(!ajax->data_kv) ajax->data_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("data"), KV_INSERT), KV_OBJECT);
	kvSetJson(kvAppend(ajax->data_kv, name, name_len, KV_REPLACE), str, str_len);
}//END: ajaxSetDataJson



/*
 * Добавление элемента в данные для клиента - Json pointer
 */
void
ajaxSetDataJsonPtr(ajax_s * ajax, const char * name, uint32_t name_len, char * str, uint32_t str_len){
	if(!ajax || !ajax->root || !name || !str) return;
	if(!ajax->data_kv) ajax->data_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("data"), KV_INSERT), KV_OBJECT);
	kvSetJsonPtr(kvAppend(ajax->data_kv, name, name_len, KV_REPLACE), str, str_len);
}//END: ajaxSetDataJsonPtr



/*
 * Очистка данных для клиента
 */
void
ajaxDataClear(ajax_s * ajax){
	if(!ajax || !ajax->data_kv) return;
	kvClear(ajax->data_kv);
}//END: ajaxDataClear



/*
 * Добавление в массив отладочной информации нового элемента
 */
void
ajaxAddDebug(ajax_s * ajax, const char * data, uint32_t data_len){
	if(!ajax || !ajax->root || !data) return;
	if(!ajax->debug_kv) ajax->debug_kv = kvSetType(kvAppend(ajax->root, CONST_STR_COMMA_LEN("debug"), KV_INSERT), KV_ARRAY);
	kvSetString(kvAppend(ajax->debug_kv, NULL, 0, KV_INSERT), data, data_len);
}//END: ajaxAddDebug



/*
 * Очистка массива отладочной информации
 */
void
ajaxDebugClear(ajax_s * ajax){
	if(!ajax || !ajax->debug_kv) return;
	kvClear(ajax->debug_kv);
}//END: ajaxDebugClear



/*
 * Помечает AJAX ответ как выполненный с ошибкой
 */
void
ajaxError(
	ajax_s * ajax, 
	const char * id, uint32_t id_len, 
	const char * title, uint32_t title_len, 
	const char * text, uint32_t text_len
){
	if(!ajax || !title || !text) return;
	ajaxSetStatus(ajax, AJAX_STATUS_ERROR);
	ajaxAddMessage(ajax, id, id_len, title, title_len, text, text_len, AJAX_MESSAGE_ERROR, AJAX_MESSAGE_WINDOW);
}//END: ajaxError



/*
 * Помечает AJAX ответ как выполненный успешно
 */
void
ajaxSuccess(
	ajax_s * ajax, 
	const char * id, uint32_t id_len, 
	const char * title, uint32_t title_len, 
	const char * text, uint32_t text_len
){
	if(!ajax || !title || !text) return;
	ajaxSetStatus(ajax, AJAX_STATUS_SUCCESS);
	ajaxAddMessage(ajax, id, id_len, title, title_len, text, text_len, AJAX_MESSAGE_SUCCESS, AJAX_MESSAGE_CLIENT);
}//END: ajaxSuccess



/*
 * Формирует AJAX ответ сервера и записывает его в очередь частей контента текущего соединения
 */
void
ajaxResponse(ajax_s * ajax){
	if(!ajax || !ajax->root || !ajax->connection) return;
	connection_s * con = ajax->connection;

	//Очистка очереди частей контента текущего соединения
	if(con->response.content){
		if(con->response.content->content_length > 0){
			chunkqueueFree(con->response.content);
			con->response.content = chunkqueueCreate();
		}
	}else{
		con->response.content = chunkqueueCreate();
	}
	chunkqueue_s * cq = con->response.content;
	buffer_s * buf = bufferCreate(response_buffer_body_increment);
	if(kvEcho(ajax->root, KVF_JSON, buf)!=NULL){
		chunkqueueAddBuffer(cq, buf, 0, buf->count, true);
	}else{
		bufferFree(buf);
	}
}//END: ajaxResponse










