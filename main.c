/***********************************************************************
 * XG SERVER
 * main.c
 * Стартовый модуль
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/


#include "core.h"
#include "event.h"
#include "kv.h"
#include "server.h"
#include "globals.h"
#include "session.h"
#include "db.h"
#include "user.h"


/*
 * Это демонстрационная функция, отвечающая за обработку запросов к /json ( http(s)://[хост]:[порт]/json )
 * Возвращает данные в JSON формате
 */
static result_e handleJson(connection_s * con){

	ajax_s * ajax = con->ajax;
	if(!ajax) con->ajax = ajax = ajaxNew(con);
	mysql_s * db = threadGetMysqlInstance("main");
	if(db){

		mysqlTemplate(db, "SELECT `user_id`,`login` FROM `users`", 0, true);

		buffer_s * body = bufferCreate(0);

		//Добавляем в JSON ответ ключ -> значение ::  "body": "Hello world!"
		//При обработке ответа на стороне клиента JavaScript, ключ - это ID элемента, а значение - контент элемента
		//Более детальное описание AJAX ответов сервера в JSON формате смотрите в файле: core/ajax.c
		ajaxAddContent(ajax,CONST_STR_COMMA_LEN("body"),CONST_STR_COMMA_LEN("Hello world!"), CONTENT_APPEND_SET);

		//Добавляем в JSON ответ ключ -> значение ::  "test": "It is JSON some value"
		ajaxAddContent(ajax,CONST_STR_COMMA_LEN("test"),CONST_STR_COMMA_LEN("It is JSON some value"), CONTENT_APPEND_SET);

		if(mysqlSelectByKeyAsJson(db, NULL, 0, ROWAS_ARRAY, false, body)==NULL){
			bufferFree(body);
		}else{
			ajaxSetDataJsonPtr(ajax, CONST_STR_COMMA_LEN("users"), body->buffer, body->count);
			mFree(body); //не bufferFree(), т.к. область памяти на которую указывает body->buffer используется и будет освобождена позже, здесь только освобождаем структуру
		}

	}else{
		DEBUG_MSG("INSTANCE [main] NOT FOUND");
	}



	return RESULT_OK;
}




/***********************************************************************
 * Функции
 **********************************************************************/

static void signalHandlerSIGPOLL(int sig){}

static void 
signalHandlerDefault(int x){
	switch(x){
		case SIGPIPE: break;
		case SIGTERM:
		case SIGINT:
		case SIGKILL:
		case SIGTSTP:
		default: 
			XG_STATUS = XGS_STOPPED;
			DEBUG_MSG("XGS_STOPPED\n");
		break;
	}
}


/*
 * Инициализация обработчиков сигналов
 */
static void
signalsInit(void){

	//SIGPOLL
	signal(SIGPOLL, &signalHandlerSIGPOLL);
	siginterrupt(SIGPOLL, 1);

	//Все остальное
	signal(SIGPIPE, &signalHandlerDefault);	//Завершение	Запись в разорванное соединение (файп, сокет)
	signal(SIGTERM, &signalHandlerDefault);	//Завершение	Сигнал завершения (сигнал по умолчанию для утилиты kill)
	signal(SIGINT, &signalHandlerDefault);	//Завершение	Сигнал прерывания (Ctrl-C) с терминала
	signal(SIGKILL, &signalHandlerDefault);	//Завершение	Безусловное завершение
	signal(SIGTSTP, &signalHandlerDefault);	//Остановка процесса	Сигнал остановки с терминала (Ctrl-Z).

	DEBUG_MSG("signalsInit complete.");
}//END: signalsInit





/*
 * Старт
 */
int main(int argc, char *argv[]){


	//Генерация события (описание в core/event.h)
	fireEvent(EVENT_LOADER_START, NULL);

	setlocale(LC_ALL, "C");

	//Обработка сигналов
	signalsInit();

	//PID процесса
	XG_PID = getpid();

	//Настройки
	XG_CONFIG = kvNewRoot();
	configReadAll("./conf");

/*
	kv_s * c = kvCopy(NULL, kvSearch(XG_CONFIG,CONST_STR_COMMA_LEN("webserver")));
	buffer_s * b = kvEcho(c, KVF_JSON, NULL);
	bufferPrint(b);
	bufferFree(b);
*/

	//Маршруты
	XG_ROUTES = kvNewRoot();
	routeAdd("/json", handleJson);	//Добавление маршрута /json и функции - обработчика handleJson

	//Алиасы маршрутов
	XG_ALIASES = kvGetByPath(XG_CONFIG,"/routes/aliases");
	if(XG_ALIASES){
		if(XG_ALIASES->type != KV_OBJECT) XG_ALIASES = NULL;
	}

	//Работа с базами данных
	dbInit();

	//Установка соединений с MySQL
	XG_MYSQL_INSTANCES = mysqlCreateAllInstances();

	//Инициализация сессий
	sessionEngineInit();

/*
	buffer_s * b = bufferCreate(0);
	bufferAddStringFormat(b, "HTTP/1.1 %d %d\r\n", 200, "OK");
	bufferPrint(b);
*/

	//Загрузка расширений
	extensionsLoad();


	//Генерация события (описание в core/event.h)
	fireEvent(EVENT_LOADER_COMPLETE, NULL);

	serverInit();

/*
	kv_s * root = kvFromJsonFile("./conf/webserver.conf", KVJF_ALLOW_ALL);
	buffer_s * b = kvEcho(root, KVF_JSON);
	bufferPrint(b);
	b = encodeUrlQuery(b->buffer, b->count, NULL);
	bufferPrint(b);
	b = decodeUrlQuery(b->buffer, b->count, NULL);
	bufferPrint(b);
	root = kvFromJsonString(b->buffer, KVJF_ALLOW_NONE);
	b = kvEcho(root, KVF_JSON);
	bufferPrint(b);
	b = kvEcho(root, KVF_URLQUERY);
	bufferPrint(b);
	root = kvFromQueryString(b->buffer);
	b = kvEcho(root, KVF_JSON);
	bufferPrint(b);
	b = kvEcho(kvSearch(root,"webserver",0), KVF_HEADERS);
	bufferPrint(b);
	*/


	//Завершение работы с базами данных
	DEBUG_MSG("dbEnd()...");
	mysqlFreeAllInstances(XG_MYSQL_INSTANCES);
	dbEnd();

	//Закрытие расширений
	DEBUG_MSG("extensionsClose()...");
	extensionsClose();

	DEBUG_MSG("exit.");
	return 0;
}//END: main
