/***********************************************************************
 * XGAME SERVER
 * core/threads.c
 * Функции пула рабочих потоков
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include "core.h"
#include "server.h"
#include "globals.h"
#include "db.h"


static void		threadPoolMain(thread_s * thread);	//Основная функция потока


static pthread_key_t	thr_mysql_instance_key;
static pthread_once_t	thr_mysql_instance_once = PTHREAD_ONCE_INIT;

static void thr_mysql_instance_once_creator(void){
	//создаётся единый на процесс ключ
	pthread_key_create( &thr_mysql_instance_key, NULL);
}



/***********************************************************************
 * Работа с пулом потоков
 **********************************************************************/

/*
 * Создание пула потоков
 */
result_e
threadPoolCreate(server_s * srv, size_t count){

	if(!count) count = server_worker_threads;
	count = min(32, server_worker_threads);

	thread_pool_s * pool 	= (thread_pool_s *)mNewZ(sizeof(thread_pool_s));
	pool->threads 			= mNewZ(count * sizeof(thread_s *));
	pool->threads_count 	= count;
	pool->threads_idle		= count;
	if(srv) srv->workers	= pool;
	pool->server 			= srv;

	if(pthread_cond_init(&(pool->condition), NULL) != 0) RETURN_ERROR(RESULT_ERROR,"pthread_cond_init fail");
	if(pthread_mutex_init(&(pool->mutex), NULL) != 0) RETURN_ERROR(false,"pthread_mutex_init fail");

	size_t i;
	for(i=0; i < count; i++){
		if((pool->threads[i] = threadCreate(pool)) == NULL) RETURN_ERROR(RESULT_ERROR, "threadToPool fail");
		pool->threads[i]->index = i;
	}

	return RESULT_OK;
}//END: threadPoolCreate



/*
 * Завершение пула потоков
 */
result_e
threadPoolFree(thread_pool_s * pool){
	size_t i;

	DEBUG_MSG("Pool free: setting thread destroy flags...");

	//Установка флагов завершения потоков
	pthread_mutex_lock(&pool->mutex);
		for(i=0; i < pool->threads_count; i++){
			if(pool->threads[i]) pool->threads[i]->destroy = true;
		}
		pthread_cond_broadcast(&pool->condition);
	pthread_mutex_unlock(&pool->mutex);

	DEBUG_MSG("Pool free: waiting destroying threads...");

	bool all_threads_destroyed = false;
	time_t w_start = time(NULL);

	//Ожидание завершения всех потоков
	while(!all_threads_destroyed){
		pthread_mutex_lock(&pool->mutex);
		all_threads_destroyed = (pool->threads_count > 0 ? false : true);
		pthread_mutex_unlock(&pool->mutex);
		sleepMilliseconds(1);
		//Даем 5 секунд на завершение работы всех потоков 
		//Если за это время потоки не завершились - будем закрывать их принудительно
		if(time(NULL) - w_start > 5) break;
	}

	//Если не все потоки были завершены (выход по таймауту) - принудительно заверршаем
	if(!all_threads_destroyed){
		pthread_mutex_lock(&pool->mutex);
		for(i=0; i < pool->threads_count; i++){
			if(pool->threads[i] && !pthread_cancel(pool->threads[i]->thread_id)){
				mFree(pool->threads[i]);
			}
		}
		pthread_mutex_unlock(&pool->mutex);
	}

	mFree(pool->threads);
	pthread_cond_destroy(&pool->condition);
	pthread_mutex_destroy(&pool->mutex);
	if(pool->server)pool->server->workers = NULL;
	mFree(pool);
	DEBUG_MSG("Pool free: free");
	return RESULT_OK;
}//END: threadPoolFree




/*
 * Создание нового потока
 */
thread_s * 
threadCreate(thread_pool_s * pool){

	pthread_attr_t	attr;

	//Инициализация аттрибутов потока
	if(pthread_attr_init(&attr) != 0) RETURN_ERROR(NULL, "pthread_attr_init error");

	//Добавление свойства аттрибуту потока
	//Создаем отсоединенный поток (без необходимости выхова pthread_detach() из потока)
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0){
		pthread_attr_destroy(&attr);
		RETURN_ERROR(NULL, "pthread set detach state error");
	}

	//Установка размера стека потока
	if(pthread_attr_setstacksize(&attr, worker_thread_stack_size) != 0){
		pthread_attr_destroy(&attr);
		RETURN_ERROR(false, "pthread set stack size error");
	}

	thread_s * thread = (thread_s *)mNewZ(sizeof(thread_s));
	thread->pool = pool;

	//Создание потока с точкой старта в функции threadMain
	if (pthread_create(&(thread->thread_id), &attr, (void*)threadPoolMain, (void*)thread) != 0){
		pthread_attr_destroy(&attr); mFree(thread);
		RETURN_ERROR(NULL, "pthread create error");
	}

	pthread_attr_destroy(&attr);

	return thread;
}//END: threadCreate




/*
 * Основная функция потока
 */
static void 
threadPoolMain(thread_s * thread){

	thread_pool_s * pool	= thread->pool;		//Указатель на пул потоков
	server_s * srv			= pool->server;		//Указатель на сервер
	joblist_s * joblist		= srv->joblist;		//Указатель на список работ
	connection_s * con		= NULL;

	DEBUG_MSG("Thread ID:%d [%d] created on server [%s]...", (int)thread->thread_id, (int)pthread_self(), srv->config.host);

	//Экземпляры соединения с MySQL
	mysql_thread_init();
	mysql_s * mysql_instances = mysqlCreateAllInstances();

	pthread_once(&thr_mysql_instance_once, thr_mysql_instance_once_creator);
	if( pthread_getspecific( thr_mysql_instance_key ) == NULL ){
		pthread_setspecific( thr_mysql_instance_key, mysql_instances);
	}

	//Бесконечный цикл пока поток не получит статус завершения работы
	do {

		//Если у потока нет работы - ожидаем
		if (thread->con == NULL){
			con = jobGet(joblist);
			pthread_mutex_lock(&pool->mutex);
			if(!con){
				//Ожидаем pthread_cond_signal
				if(pthread_cond_wait(&pool->condition, &pool->mutex) != 0){
					con = NULL;
				} else {
					con = jobGet(joblist);
				}
			}
			pthread_mutex_unlock(&pool->mutex);
		}
		//У потока есть задание - выполняем
		else{
			con = thread->con;
			thread->con = NULL;
		}

		if(thread->destroy) break;

		//Есть соединение для обработки
		if(con != NULL){

			pthread_mutex_lock(&pool->mutex);
			pool->threads_idle--;
			pthread_mutex_unlock(&pool->mutex);

			if(con->stage > CON_STAGE_NONE && con->stage < CON_STAGE_COMPLETE) threadConnectionEngine(con);


			//Задание выполнено
			//Добавление соединения в список обработки основного потока
			jobmainAdd(con);

			//Генерируем сигнал SIGPOLL, чтобы прервать poll операцию в
			//основном потоке
			pthread_mutex_lock(&pool->mutex);

				pool->threads_idle++;
				write(srv->pipe[1], "", 1);

			pthread_mutex_unlock(&pool->mutex);


			//DEBUG_MSG("Thread ID:%d sleeping..., threads_idle=%u", (int)thread->thread_id, (uint32_t)pool->threads_idle);
		}

	}while(thread->destroy == false);

	//Завершение работы потока и
	//удаление потока из пула потоков
	pthread_mutex_lock(&pool->mutex);
		pool->threads_count--;
		if(pool->threads[pool->threads_count] != pool->threads[thread->index]) pool->threads[thread->index] = pool->threads[pool->threads_count];
		pool->threads[pool->threads_count] = NULL;
	pthread_mutex_unlock(&pool->mutex);


	DEBUG_MSG("Thread ID:%d [%d] stopped...", (int)thread->thread_id, (int)pthread_self());

	mFree(thread);
	mysqlFreeAllInstances(mysql_instances);
	mysql_thread_end();
	pthread_exit(NULL);
}//threadPoolMain



/*
 * Посылает сигнал для "пробуждения" потока, т.к. появилось задание
 */
inline void
threadWakeup(thread_pool_s * pool){
	if(!pool) return;
	pthread_mutex_lock(&pool->mutex);
		if (pthread_cond_signal(&pool->condition) == 0){
			//pool->threads_idle--;
		}
	pthread_mutex_unlock(&pool->mutex);
}//END: threadWakeup



#define THR_STAGE_RETURN(n_stage, n_result)do{connectionSetStage(con, n_stage); return n_result;}while(0) 

/*
 * Обработка соединения рабочим потоком согласно его текущего статуса
 */
result_e
threadConnectionEngine(connection_s * con){

	register server_s * srv = con->server;
	result_e result;

	while(1){
		switch(con->stage){

			//Получение данных от клиента
			case CON_STAGE_READ:

				do{
					if(con->ssl){
						result = connectionHandleReadSSL(con);
					}else{
						result = connectionHandleRead(con);
					}
				}while(result == RESULT_OK);

				switch(result){
					//Ошибка сокета
					case RESULT_ERROR:
						con->connection_error = CON_ERROR_READ_SOCKET;
						THR_STAGE_RETURN(CON_STAGE_SOCKET_ERROR, RESULT_OK);
					break;
					case RESULT_EOF:		//Данных больше нет (из-за какой-то ошибки или сбоя)
					case RESULT_CONRESET:	//Соединение разорвано
						con->connection_error = CON_ERROR_DISCONNECT;
						THR_STAGE_RETURN(CON_STAGE_CLOSE, RESULT_OK);
					break;
					//Выполнено упешно
					case RESULT_COMPLETE:
						connectionSetStage(con, CON_STAGE_WORKING);
					break;
					//Повторить и прочее
					case RESULT_AGAIN:
					default:
						con->read_idle_ts = srv->current_ts;
						return RESULT_OK;
					break;
				}

			break;


			//Ожидание ответа от сервера
			case CON_STAGE_WORKING:

				//Если при обработке запроса возникла ошибка
				if(con->http_code != 200){
					switch(con->http_code){
						case 301: case 302:
						//Эти коды устанавливаются и обрабатываются только в функции responseHttpLocation()
						break;
						default: responseHttpError(con);
					}
					THR_STAGE_RETURN(CON_STAGE_BEFORE_WRITE, RESULT_OK);
					break;
				}

				if(!con->response.headers) con->response.headers = kvNewRoot();
				if(!con->response.cookie) con->response.cookie = kvNewRoot();

				//Получение GET параметров запроса из URI query string
				if(con->request.uri.query.ptr && con->request.uri.query.len > 0){
					con->request.get = kvFromQueryString(con->request.uri.query.ptr);
				}

				//Если POST запрос - обработка
				if(con->request.request_method == HTTP_POST){
					//multipart/form-data
					if(con->request.post_method == POST_MULTIPART){
						requestParseMultipartForm(con);
					}
					//application/x-www-form-urlencoded
					else{
						requestParseUrlEncodedForm(con);
					}
				}//Обработка POST запроса

				//Проверка, является ли запрос AJAX запросом
				if(!con->request.is_ajax){
					const char * ajax = requestGetGPC(con, "ajax", "gp", NULL);
					if(ajax && (stringCompare(ajax,"1")||stringCompareCase(ajax,"true")||stringCompareCase(ajax,"on"))) con->request.is_ajax = true;
				}

				route_cb f = routeGet(con->request.uri.path.ptr);

				//Если найден обработчик маршрута URI
				if(f){

					//Если запрос является AJAX запросом,
					//то контент генерируется "на лету", поэтому добавляем заголовки,
					//запрещающие кэширование ответа сервера
					responseSetHeader(&con->response, "Pragma", "no-cache", KV_REPLACE);
					responseSetHeader(&con->response, "Expires", "Mon, 26 Jul 1997 05:00:00 GMT", KV_REPLACE);
					responseSetHeader(&con->response, "Cache-Control", "no-store, no-cache, must-revalidate", KV_REPLACE);
					if(con->request.is_ajax){
						responseSetHeader(&con->response, "Cache-Control", "post-check=0, pre-check=0", KV_INSERT);
						con->ajax = ajaxNew(con);
					}

					//Старт сессии
					con->session = sessionStart(requestGetGPC(con, sessionGetName(), "cpg", NULL));
					//Проверка принадлежности текущей сессии клиенту
					uint32_t uagent_hash = hashString(con->request.user_agent.ptr, NULL);
					if(!sessionIsValidClient(con->session, &con->remote_addr, uagent_hash)){
						sessionClose(con->session);
						con->session = sessionNew(NULL);
						sessionIsValidClient(con->session, &con->remote_addr, uagent_hash);
					}


					if((result = f(con)) == RESULT_OK){
						//Если сессия была создана и были заданы переменные внутри сессии - добавляем в ответ Cookie и ID сессии
						if(con->session && BIT_ISSET(con->session->state, SESSION_CREATED) && BIT_ISSET(con->session->state, SESSION_CHANGED)) responseSetCookie(&con->response, sessionGetName(), con->session->session_id);

						//Если AJAX запрос
						if(con->ajax){
							//Здесь должно быть преобразование данных структуры ajax_s в вывод chunkqueue_s
							//Все данные, ранее записанные в chunkqueue_s сбрасываются
							ajaxResponse(con->ajax);
						}
						responseBuildHeaders(con);
					}

					//Если была создана структура AJAX ответа - освобождаем ее
					if(con->ajax){
						ajaxFree(con->ajax);
						con->ajax = NULL;
					}

					//Если была открыта сессия - закрываем с сохранением
					if(con->session){
						sessionClose(con->session);
						con->session = NULL;
					}

				}else{
					//При AJAX запросе идет запрос статичного файла? хм...
					if(con->request.is_ajax == true){
						result = RESULT_ERROR;
						con->http_code = 400;
						break;
					}
					con->request.static_file = requestStaticFileInfo(con);
					if(con->request.static_file){
						if(BIT_ISSET(con->request.headers_bits,HEADER_IF_NONE_MATCH) &&
						stringCompare(con->request.static_file->etag->ptr, con->request.if_none_match.ptr)){
							con->http_code = 304;
							break;
						}
						result = responseStaticFile(con, con->request.static_file);

					}else{
						result = RESULT_ERROR;
						con->http_code = 404;
						break;
					}
				}

				if(result != RESULT_OK) break;

				THR_STAGE_RETURN(CON_STAGE_BEFORE_WRITE, RESULT_OK);

			break;



			//Отправка ответа клиенту
			case CON_STAGE_WRITE:

				do{
					if(con->ssl){
						result = connectionHandleWriteSSL(con);
					}else{
						result = connectionHandleWrite(con);
					}
				}while(result == RESULT_OK);

				switch(result){
					//Ошибка сокета
					case RESULT_ERROR:
						con->connection_error = CON_ERROR_WRITE_SOCKET;
						THR_STAGE_RETURN(CON_STAGE_SOCKET_ERROR, RESULT_OK);
					break;
					//Соединение разорвано
					case RESULT_CONRESET:
						con->connection_error = CON_ERROR_DISCONNECT;
						THR_STAGE_RETURN(CON_STAGE_CLOSE, RESULT_OK);
					break;
					//Данных больше нет - все данные успешно отправлены
					case RESULT_EOF:	//<-- в данном случае записи RESULT_EOF говорит о том, что все данные отправлены и нет больше данных для отправки, не является ошибкой
					case RESULT_COMPLETE:
						THR_STAGE_RETURN(CON_STAGE_COMPLETE, RESULT_OK);
					break;
					//Повторить и прочее
					case RESULT_AGAIN:
					default:
						return RESULT_OK;
					break;
				}

			break;

			default: return RESULT_OK;
		}
	}

	return RESULT_OK;
}//END: threadConnectionEngine



/*
 * Получение экземпляра соединения с базой данных MySQL для текущего потока
 */
mysql_s *
threadGetMysqlInstance(const char * instance_name){
	mysql_s * instance = (mysql_s *)pthread_getspecific(thr_mysql_instance_key);
	if(!instance_name) return mysqlClear(instance);
	for(; instance != NULL; instance = instance->next){
		if(stringCompareCase(instance->instance_name, instance_name)) return mysqlClear(instance);
	}
	return NULL;
}//END: threadGetMysqlInstance

