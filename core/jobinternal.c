/***********************************************************************
 * XGAME SERVER
 * core/jobinternal.c
 * Работа со списком внутренних заданий сервера
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include "core.h"
#include "kv.h"
#include "server.h"
#include "session.h"
#include "globals.h"


static pthread_mutex_t	job_internal_mutex;
static pthread_cond_t	job_internal_condition;		//Условие

static jobinternal_s 	* _internal_idle_list	= NULL;
static jobinternal_s 	* jobinternal_list 		= NULL;
static bool				jobinternal_thread_destroyed = false;
static pthread_t		jobinternal_thread_id;

static void 	jobinternalThreadMain(void * data);
static void		jobinternalThreadCreate(void);


#define _toIdle(f_name, d_type, d_list, d_mutex) static void f_name(d_type * item){	\
	if(!item) item = (d_type *)mNewZ(sizeof(d_type));	\
	if(item->free) item->free(item->data);	\
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
_toIdle(_internalToIdle, jobinternal_s, _internal_idle_list, job_internal_mutex);

//Получает элемент из IDLE списка или создает новый, если IDLE список пуст
_fromIdle(_internalFromIdle, jobinternal_s, _internal_idle_list, job_internal_mutex);



/**
 * Инициализация jobinternal.c
 */
initialization(jobinternal_c){

	pthread_mutexattr_t mutex_attr;
	pthread_mutexattr_init(&mutex_attr);
	pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&job_internal_mutex, &mutex_attr);
	pthread_cond_init(&job_internal_condition, NULL);

	int i;
	for(i=0;i<32;i++) _internalToIdle(NULL);

	jobinternalThreadCreate();

	DEBUG_MSG("jobinternal.c initialized.");
}//END: initialization



/**
 * Завершение работы
 */
finalization(jobinternal_c){
	pthread_mutex_destroy(&job_internal_mutex);
	pthread_cond_destroy(&job_internal_condition);
}//END: finalization




/***********************************************************************
 * Работа со списком внутренних заданий сервера
 **********************************************************************/ 


/*
 * Добавление внутреннего рабочего задания для сервера
 * Функция вызывается только основным потоком
 */
void
jobinternalAdd(jobinternal_e type, void * data, free_cb cb){

	pthread_mutex_lock(&job_internal_mutex);
		jobinternal_s * item = _internalFromIdle();
		item->type		= type;
		item->data		= data;
		item->ignore	= false;
		item->free		= cb;
		if(jobinternal_list) jobinternal_list->next = item;
		jobinternal_list = item;
	pthread_mutex_unlock(&job_internal_mutex);

	jobinternalWakeup();

	return;
}//END: jobinternalAdd



/*
 * Возвращает первое на очереди задание, одновременно удаляя его из списка заданий
 */
jobinternal_s *
jobinternalGet(void){
	jobinternal_s * item = NULL;

	pthread_mutex_lock(&job_internal_mutex);

		while(jobinternal_list){
			item = jobinternal_list;
			jobinternal_list = item->next;
			if(item->ignore){
				_internalToIdle(item);
				continue;
			}
		}
	pthread_mutex_unlock(&job_internal_mutex);

	return item;
}//END: jobinternalGet






/***********************************************************************
 * Рабочий поток
 **********************************************************************/ 

/*
 * Посылает сигнал для "пробуждения" потока, т.к. появилось задание
 */
inline void
jobinternalWakeup(void){
	pthread_mutex_lock(&job_internal_mutex);
		pthread_cond_signal(&job_internal_condition);
	pthread_mutex_unlock(&job_internal_mutex);
}//END: jobinternalWakeup




/*
 * Основная функция потока
 */
static void 
jobinternalThreadMain(void * data){

	//Бесконечный цикл пока поток не получит статус завершения работы
	do {

		jobinternal_s * item  = jobinternalGet();
		pthread_mutex_lock(&job_internal_mutex);
		if(!item){
			//Ожидаем pthread_cond_signal
			if(pthread_cond_wait(&job_internal_condition, &job_internal_mutex) != 0){
				item = NULL;
			} else {
					item = jobinternalGet();
			}
		}
		pthread_mutex_unlock(&job_internal_mutex);

		if(XG_STATUS != XGS_WORKING) break;

		//Есть задание для обработки
		if(item != NULL){

			switch(item->type){
				case JOB_INTERNAL_SESSION_CLEANER:
					sessionDeleteExpired();
				break;
				default:
				break;
			}

			_internalToIdle(item);

		}//Есть задание для обработки

	}while(XG_STATUS == XGS_WORKING);

	jobinternal_thread_destroyed = true;

	pthread_exit(NULL);
}//threadMain





/*
 * Создание потока
 */
static void
jobinternalThreadCreate(void){

	pthread_attr_t	attr;

	//Инициализация аттрибутов потока
	if(pthread_attr_init(&attr) != 0) FATAL_ERROR("pthread_attr_init error");

	//Добавление свойства аттрибуту потока
	//Создаем отсоединенный поток (без необходимости выхова pthread_detach() из потока)
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0){
		pthread_attr_destroy(&attr);
		FATAL_ERROR("pthread set detach state error");
	}

	//Установка размера стека потока
	if(pthread_attr_setstacksize(&attr, worker_thread_stack_size) != 0){
		pthread_attr_destroy(&attr);
		FATAL_ERROR("pthread set stack size error");
	}

	//Создание потока с точкой старта в функции threadMain
	if (pthread_create(&jobinternal_thread_id, &attr, (void*)jobinternalThreadMain, NULL) != 0){
		pthread_attr_destroy(&attr);;
		FATAL_ERROR("pthread create error");
	}

	pthread_attr_destroy(&attr);

	return;
}//END: threadCreate



/*
 * Завершение рабочего потока сервера
 */
void
jobinternalThreadFree(void){

	//Установка флагов завершения потоков
	pthread_mutex_lock(&job_internal_mutex);
		pthread_cond_broadcast(&job_internal_condition);
	pthread_mutex_unlock(&job_internal_mutex);

	time_t w_start = time(NULL);

	//Ожидание завершения всех потоков
	while(!jobinternal_thread_destroyed){
		sleepMilliseconds(1);
		//Даем 5 секунд на завершение работы всех потоков 
		//Если за это время потоки не завершились - будем закрывать их принудительно
		if(time(NULL) - w_start > 5) break;
	}

	//Если не все потоки были завершены (выход по таймауту) - принудительно заверршаем
	if(!jobinternal_thread_destroyed){
		pthread_mutex_lock(&job_internal_mutex);
			pthread_cancel(jobinternal_thread_id);
		pthread_mutex_unlock(&job_internal_mutex);
	}

	return;
}//END: jobinternalThreadFree
