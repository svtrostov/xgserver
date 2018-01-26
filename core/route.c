/***********************************************************************
 * XG SERVER
 * core/route.c
 * Функции управления обработчиками запросов
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include "server.h"
#include "globals.h"




/***********************************************************************
 * Функции
 **********************************************************************/

/*
 * Добавляет функцию-обработчик запроса для обработки определенного маршрута
 */
bool
routeAdd(const char * path, route_cb v_function){
	if(!path || !v_function) return false;
	kvSetFunctionByPath(XG_ROUTES, path, v_function);
	return true;
}//END: routeAdd



/*
 * Ищет функцию-обработчик запроса для обработки определенного маршрута
 */
route_cb
routeGet(const char * path){
	if(!path) return NULL;
	return (route_cb)kvGetFunctionByPath(XG_ROUTES, path, NULL);
}//END: routeGet











