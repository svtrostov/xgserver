/***********************************************************************
 * XG SERVER
 * Глобальные переменные
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/

#ifndef _XGGLOBALS_H
#define _XGGLOBALS_H

#include "core.h"		//Ядро
#include "kv.h"			//KV
#include "db.h"			//DB
#include "server.h"


//Переменные конфигурации
kv_s * XG_CONFIG;

//Маршруты обработки запросов
kv_s * XG_ROUTES;

//Алиасы маршрутов
kv_s * XG_ALIASES;


//Статус HTTPS сервера
server_status_e XG_STATUS;

//PID процесса
pid_t XG_PID;

//Соединения с MySQL
mysql_s * XG_MYSQL_INSTANCES;


//Ключ потоков для хранения экземпляров соединений с базой данных
pthread_key_t XG_THREAD_MYSQL_INSTANCES_KEY;


#endif //_XGGLOBALS_H 
