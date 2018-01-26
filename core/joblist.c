/***********************************************************************
 * XG SERVER
 * core/joblist.c
 * Работа со списком заданий для рабочих потоков и основного потока
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include "core.h"
#include "server.h"
#include "globals.h"

//Мьютекс синхронизации в момент изменения состояния рабочего статуса соединения
static pthread_mutex_t job_mutex = PTHREAD_MUTEX_INITIALIZER;


/***********************************************************************
 * Работа со списком заданий для рабочих потоков
 * Основной поток направляет соединения рабочим потокам
 **********************************************************************/ 



/*
 * Добавляет новый/существующмй элемент в IDLE список
 */
static void
_joblistItemToIdle(joblist_s * list, jobitem_s * item){
	if(!item) item = (jobitem_s *)mNew(sizeof(jobitem_s));
	item->next = list->idle;
	list->idle = item;
}//END: _joblistItemToIdle



/*
 * Получает элемент из IDLE списка или создает новый, если IDLE список пуст
 */
static jobitem_s *
_joblistItemFromIdle(joblist_s * list){
	jobitem_s * item;
	if(list->idle){
		item = list->idle;
		list->idle = item->next;
	}else{
		item = (jobitem_s *)mNew(sizeof(jobitem_s));
	}
	return item;
}//END: _joblistItemFromIdle



/*
 * Создание списка рабочих заданий
 */
result_e
joblistCreate(server_s * srv){
	joblist_s * joblist 	= (joblist_s *)mNewZ(sizeof(joblist_s));
	if(srv) srv->joblist	= joblist;
	joblist->server 		= srv;
	int i;
	for(i=0;i<server_joblist_size; i++) _joblistItemToIdle(joblist, NULL);
	return RESULT_OK;
}//END: joblistCreate



/*
 * Уничтожение списка рабочих заданий
 */
result_e
joblistFree(joblist_s * list){
	jobitem_s * item;
	jobitem_s * next;

	next = list->first;
	while(next){
		item = next;
		next = item->next;
		mFree(item);
	}

	next = list->idle;
	while(next){
		item = next;
		next = item->next;
		mFree(item);
	}

	mFree(list);
	return RESULT_OK;
}//END: joblistFree




/*
 * Добавление соединения в список заданий для обработки рабочим потоком
 * Функция вызывается только основным потоком
 */
void
jobAdd(connection_s * con){

	joblist_s * joblist = con->server->joblist;
	if(!joblist) return;

	pthread_mutex_lock(&job_mutex);

		//Если соединение еще не находится в списке заданий - добавляем соединение в список заданий
		if(connectionSetJobStage(con, JOB_STAGE_WAITING, false) == JOB_STAGE_WAITING){

			jobitem_s * item = _joblistItemFromIdle(joblist);
			item->connection	= con;
			item->next			= NULL;
			item->ignore		= false;
			con->job_item		= item;

			if(joblist->last) joblist->last->next = item;
			if(!joblist->first) joblist->first = item;
			joblist->last = item;

		}

	pthread_mutex_unlock(&job_mutex);

	threadWakeup(con->server->workers);

	return;
}//END: jobAdd



/*
 * Возвращает первое на очереди задание, одновременно удаляя его из списка заданий
 * Функция запрашивается только рабочими потоками 
 */
connection_s *
jobGet(joblist_s * joblist){
	if(!joblist) return NULL;
	jobitem_s * item;
	connection_s * con = NULL;

	pthread_mutex_lock(&job_mutex);

		while(joblist->first){
			item = joblist->first;
			joblist->first = item->next;
			if(joblist->last == item) joblist->last = NULL;

			//Если текущее задание должно быть проигнорировано (такое возникает в случае, когда соединение находится в процессе удаления)
			if(item->ignore){
				_joblistItemToIdle(joblist, item);
				con = NULL;
				continue;
			}

			con = item->connection;
			if(con->job_item != item){
				_joblistItemToIdle(joblist, item);
				con = NULL;
				continue;
			}

			con->job_item = NULL;
			_joblistItemToIdle(joblist, item);

			//Если текущее соединение не используется или его обработка завершена или 
			//не удается задать стадию соединения как рабочую - игнорируем соединение
			if(
				con->stage == CON_STAGE_NONE ||
				con->stage >= CON_STAGE_COMPLETE || 
				connectionSetJobStage(con, JOB_STAGE_WORKING, false) != JOB_STAGE_WORKING
			){
				con = NULL;
				continue;
			}
			break;
		}

	pthread_mutex_unlock(&job_mutex);
	return con;
}//END: jobGet



/*
 * Удаляет соединение из списка рабочих заданий
 * Функция запрашивается основным потоком
 */
bool
jobDelete(connection_s * con){
	bool can_delete = true;
	pthread_mutex_lock(&job_mutex);

		if(con->job_item) con->job_item->ignore = true;
		con->job_item = NULL;
		if(con->job_stage == JOB_STAGE_WORKING){
			can_delete = false;
		}else{
			connectionSetJobStage(con, JOB_STAGE_NONE, true);
		}

	pthread_mutex_unlock(&job_mutex);
	return can_delete;
}//END: jobDelete




/***********************************************************************
 * Работа со списком заданий для основного потока
 * Рабочие потоки направляют соединения основному потоку после обработки
 * Функции вызываются только из рабочих потоков
 **********************************************************************/ 


/*
 * Создание списка заданий для основного потока
 */
result_e
jobmainCreate(server_s * srv){
	joblist_s * jobmain 	= (joblist_s *)mNewZ(sizeof(joblist_s));
	if(srv) srv->jobmain	= jobmain;
	jobmain->server 		= srv;
	int i;
	for(i=0;i<server_joblist_size; i++) _joblistItemToIdle(jobmain, NULL);
	return RESULT_OK;
}//END: jobmainCreate




/*
 * Уничтожение списка заданий
 */
result_e
jobmainFree(joblist_s * jobmain){
	return joblistFree(jobmain);
}//END: jobmainFree



/*
 * Добавление соединения в список заданий для обработки основным потоком
 */
void
jobmainAdd(connection_s * con){

	joblist_s * jobmain = con->server->jobmain;
	if(!jobmain) return;

	pthread_mutex_lock(&job_mutex);

		//Если соединение еще не находится в списке заданий - добавляем соединение в список заданий
		if(connectionSetJobStage(con, JOB_STAGE_WAITMAIN, false) == JOB_STAGE_WAITMAIN){

			jobitem_s * item = _joblistItemFromIdle(jobmain);
			item->connection	= con;
			item->next			= NULL;
			item->ignore		= false;
			con->job_item		= item;

			if(jobmain->last) jobmain->last->next = item;
			if(!jobmain->first) jobmain->first = item;
			jobmain->last = item;

		}

	pthread_mutex_unlock(&job_mutex);

	return;
}//END: jobmainAdd




/*
 * Возвращает первое на очереди задание, одновременно удаляя его из списка заданий
 * Функция запрашивается основным потоком
 */
connection_s *
jobmainGet(joblist_s * jobmain){
	if(!jobmain) return NULL;
	jobitem_s * item;
	connection_s * con = NULL;

	pthread_mutex_lock(&job_mutex);

		while(jobmain->first){
			item = jobmain->first;
			jobmain->first = item->next;
			if(jobmain->last == item) jobmain->last = NULL;

			//Если текущее задание должно быть проигнорировано (такое возникает в случае, когда соединение находится в процессе удаления)
			if(item->ignore){
				_joblistItemToIdle(jobmain, item);
				con = NULL;
				continue;
			}

			con = item->connection;
			if(con->job_item != item){
				_joblistItemToIdle(jobmain, item);
				con = NULL;
				continue;
			}

			con->job_item = NULL;
			_joblistItemToIdle(jobmain, item);

			if(
				con->stage == CON_STAGE_NONE ||
				connectionSetJobStage(con, JOB_STAGE_NONE, false) != JOB_STAGE_NONE
			){
				con = NULL;
				continue;
			}
			break;
		}

	pthread_mutex_unlock(&job_mutex);
	return con;
}//END: jobmainGet





/***********************************************************************
 * Работа со списком внутренних заданий для рабочих потоков
 * Основной поток направляет соединения рабочим потокам
 **********************************************************************/ 


