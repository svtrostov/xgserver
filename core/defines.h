/***********************************************************************
 * XG SERVER
 * Декларации
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/

#ifndef _XGDEFINES_H
#define _XGDEFINES_H

#define _REENTRANT
#define USE_POLL



//декталация "XG_DEBUG" разрещает вывод сообщений отладки на экран во время исполнения программы
#define XG_DEBUG

//декталация "XG_ERROR" разрещает вывод на экран сообщений об ошибках, возникующих  во время исполнения программы
#define XG_ERROR

//декталация "XG_MEMSTAT" разрещает сбор статистики по выделенной/освобожденной памяти программы - используется для отладки
#define XG_MEMSTAT0

//декталация "XG_MEMSTAT_BACKTRACE" работает только при декларированном "XG_MEMSTAT" и выводит backtrace при каждом вызове mNew mNewZ  и т.д.
#define XG_MEMSTAT_BACKTRACE0

//декталация "XG_CONSTAT" разрещает вывод на экран статистики соединений - используется для отладки
#define XG_CONSTAT0


//декталация "XG_MEM_USE_CACHE" разрешает приложению использовать собственный кеш маленьких блоков данных 24, 40, 56, 72, 88, 104 байт <- замедняет работу, не использовать
#define XG_MEM_USE_CACHE0

#endif //_XGDEFINES_H 
