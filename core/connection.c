/***********************************************************************
 * XG SERVER
 * core/connection.c
 * Работа с соединениями
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include "server.h"
#include "globals.h"


//Буфер-мусорка
//static char trash_buffer[1024 * 16];


//Мьютекс синхронизации в момент изменения состояния соединения
static pthread_mutex_t connection_stage_mutex = PTHREAD_MUTEX_INITIALIZER;

//Инкрементальный ID соединения
static uint64_t connection_unique_id = 0;



/***********************************************************************
 * Работа с соединениями connection_s
 **********************************************************************/ 


/*
 * Возвращает статус соединения в виде строки
 */
const char *
connectionStageAsString(connection_stage_e s){
	switch(s){

		//Начальная стадия
		case CON_STAGE_NONE: return "CON_STAGE_NONE";

		//Установка соединения
		case CON_STAGE_ACCEPTING: return "CON_STAGE_ACCEPTING";
		case CON_STAGE_HANDSTAKE: return "CON_STAGE_HANDSTAKE";
		case CON_STAGE_CONNECTED: return "CON_STAGE_CONNECTED";

		//Чтение данных запроса
		case CON_STAGE_READ: return "CON_STAGE_READ";

		//Подготовка и отправка ответа 
		case CON_STAGE_WORKING: return "CON_STAGE_WORKING";
		case CON_STAGE_BEFORE_WRITE: return "CON_STAGE_BEFORE_WRITE";
		case CON_STAGE_WRITE: return "CON_STAGE_WRITE";

		//Успешное завершение соединения
		case CON_STAGE_COMPLETE: return "CON_STAGE_COMPLETE";

		//Ошибки соединения
		case CON_STAGE_ERROR: return "CON_STAGE_ERROR";
		case CON_STAGE_SOCKET_ERROR: return "CON_STAGE_SOCKET_ERROR";

		//Закрытие соединения
		case CON_STAGE_CLOSE: return "CON_STAGE_CLOSE";

		//После закрытия соединения
		case CON_STAGE_DESTROYING: return "CON_STAGE_DESTROYING";
		case CON_STAGE_CLOSED: return "CON_STAGE_CLOSED";

		default: return "UNDEFINED CONNECTION STAGE";
	}
}//END: connectionStageAsString



/*
 * Возвращает текстовое описание ошибки соединения
 */
const char *
connectionErrorAsString(connection_error_e e){
	switch(e){
		case CON_ERROR_NONE: return "CON_ERROR_NONE";
		case CON_ERROR_DISCONNECT: return "CON_ERROR_DISCONNECT";
		case CON_ERROR_TIMEOUT: return "CON_ERROR_TIMEOUT";
		case CON_ERROR_UNDEFINED_STAGE: return "CON_ERROR_UNDEFINED_STAGE";

		case CON_ERROR_ACCEPT_REQUEST: return "CON_ERROR_ACCEPT_REQUEST";
		case CON_ERROR_ACCEPT_SOCKET: return "CON_ERROR_ACCEPT_SOCKET";
		case CON_ERROR_ACCEPT_TIMEOUT: return "CON_ERROR_ACCEPT_TIMEOUT";

		case CON_ERROR_HANDSTAKE_SSL_CREATE: return "CON_ERROR_HANDSTAKE_SSL_CREATE";
		case CON_ERROR_HANDSTAKE_SSL_SET_FD: return "CON_ERROR_HANDSTAKE_SSL_SET_FD";
		case CON_ERROR_HANDSTAKE_SOCKET: return "CON_ERROR_HANDSTAKE_SOCKET";
		case CON_ERROR_HANDSTAKE_TIMEOUT: return "CON_ERROR_HANDSTAKE_TIMEOUT";

		case CON_ERROR_READ_SOCKET: return "CON_ERROR_READ_SOCKET";
		case CON_ERROR_WRITE_SOCKET: return "CON_ERROR_WRITE_SOCKET";

		case CON_ERROR_FDEVENT_SOCKET: return "CON_ERROR_FDEVENT_SOCKET";
		case CON_ERROR_FDEVENT_UNDEFINED: return "CON_ERROR_FDEVENT_UNDEFINED";
		default: return "UNDEFINED CONNECTION ERROR";
	}
}//END: connectionErrorAsString



/*
 * Устанавливает новый этап жизненного цикла соединения
 */
connection_stage_e
connectionSetStage(connection_s * con, connection_stage_e new_stage){
	//При изменении состояния соединения используются мьютексы,
	//поскольку сосотояние соединения может быть изменено помимо основного потока также рабочими потоками
	pthread_mutex_lock(&connection_stage_mutex);
	//Жизненный цикл соединения устанавливается только в сторону увеличения
	if(con->stage < new_stage) con->stage = new_stage;
	new_stage = con->stage;
	pthread_mutex_unlock(&connection_stage_mutex);
	return new_stage;
}//END: connectionSetStage



/*
 * Устанавливает новый этап обработки соединения рабочим потоком
 */
job_stage_e
connectionSetJobStage(connection_s * con, job_stage_e new_stage, bool necessarily){
	job_stage_e result;
	//При изменении состояния соединения используются мьютексы,
	//поскольку сосотояние соединения может быть изменено помимо основного потока также 
	pthread_mutex_lock(&connection_stage_mutex);
	if(necessarily){
		con->job_stage = new_stage;
	}else{
		switch(new_stage){
			case JOB_STAGE_NONE:
				if(con->job_stage == JOB_STAGE_WAITMAIN) con->job_stage = new_stage;
			break;
			case JOB_STAGE_WAITING:
				if(con->job_stage == JOB_STAGE_NONE) con->job_stage = new_stage;
			break;
			case JOB_STAGE_WORKING:
				if(con->job_stage == JOB_STAGE_WAITING) con->job_stage = new_stage;
			break;
			case JOB_STAGE_WAITMAIN:
				if(con->job_stage == JOB_STAGE_WORKING) con->job_stage = new_stage;
			break;
		}
	}
	result = con->job_stage;
	pthread_mutex_unlock(&connection_stage_mutex);

	return result;
}//END: connectionSetJobStage




/*
 * Создание структуры клиентского соединения
 */
static connection_s *
connectionStructureCreate(server_s * srv){
	connection_s * con = (connection_s *) mNewZ(sizeof(connection_s));
	con->fd			= -1;	//Дескриптор соединения
	con->index		= -1;	//Индекс в массиве fds
	con->stage		= CON_STAGE_NONE;
	con->server		= srv;	//Ссылка на структуру сервера
	con->http_code	= 200;	//HTTP статус обработки запроса (код ответа)
	con->job_stage	= JOB_STAGE_NONE;
	con->job_item	= NULL;
	return con;
}//END: connectionStructureCreate




/*
 * Создание массива структур клиентских соединений
 */
result_e
connectionsCreate(server_s * srv){
	size_t i;
	srv->connections		= (connection_s **)mNewZ(sizeof(connection_s *) * server_max_connections);
	srv->connections_count	= 0;
	for(i = 0; i < server_max_connections; i++){
		srv->connections[i] = connectionStructureCreate(srv);
	}
	return RESULT_OK;
}//END: connectionsArrayCreate



/*
 * Удаление массива структур клиентских соединений
 */
result_e
connectionsFree(server_s * srv){
	size_t i;
	connection_s * con;
	for(i = 0; i < server_max_connections; i++){
		con = srv->connections[i];
		connectionClear(con);
		mFree(con);
	}
	mFree(srv->connections);
	srv->connections		= NULL;
	srv->connections_count	= 0;
	return RESULT_OK;
}//END: connectionsFree



/*
 * Вывод на экран состояния активных соединений по состояниям
 */
void
connectionsPrint(server_s * srv){
	size_t i;
	uint32_t stages[CON_STAGE_DESTROYING + 1];
	memset(stages,'\0',sizeof(stages));
	connection_s * con;
	printf("Total connections: %" PRIu64 "\n",connection_unique_id);
	printf("Active connections: %u\n",srv->connections_count);
	for(i = 0; i < srv->connections_count; i++){
		con = srv->connections[i];
		stages[(int)con->stage]++;
	}
	for(i = CON_STAGE_NONE; i <= CON_STAGE_DESTROYING; i++){
		if(stages[i]>0) printf("%s\t = [%d]\n", connectionStageAsString(i), stages[i]);
	}
	printf("\n");
}//END: connectionsPrint



/*
 * Возвращает экземпляр доступного для использования клиентского соединения или NULL, если нет доступных соединений
 */
connection_s *
connectionGet(server_s * srv){
	if(srv->connections_count >= server_max_connections) RETURN_ERROR(NULL,"srv->connections_count >= server_max_connections");	//Достигнут лимит на количество установленных соединений
	connection_s * con = srv->connections[srv->connections_count];
	if(con->stage != CON_STAGE_NONE) FATAL_ERROR("con->stage != CON_STAGE_NONE");
	con->index = srv->connections_count;
	con->connection_id = connection_unique_id++;
	srv->connections_count++;
	return con;
}//END: connectionGet



/*
 * Возвращает последний ID соединения
 */
uint64_t
connectionGetLastId(void){
	return connection_unique_id;
}//END: connectionGetLastId




/*
 * Сбрасывает структуру клиентского соединения до начальных параметров
 */
result_e
connectionClear(connection_s * con){

	requestClear(&(con->request));		//Обнуление структуры request_s (запрос)
	responseClear(&(con->response));	//Обнуление структуры response_s (ответ)

	//Освобождение памяти, занятой под SSL
	if(con->ssl) SSL_free(con->ssl);

	//Освобождение памяти, занятой под сессию клиента
	if(con->session) sessionClose(con->session);

	memset(con, '\0', sizeof(connection_s));
	return RESULT_OK;
}//END: connectionClear



/*
 * "Удаляет" соединение (фактически структура не удаляется, а сбрасывается до начального состояния)
 */
result_e
connectionDelete(connection_s * con){

	if(!con) FATAL_ERROR("!con");
	if(!con->server) FATAL_ERROR("!con->server");
	if(con->index < 0) FATAL_ERROR("con->index < 0");
	server_s * srv = con->server;
	if(srv->connections[con->index]->index != con->index) FATAL_ERROR("srv->connections[i] != con");
	if(!srv->connections_count) FATAL_ERROR("!srv->connections_count");

	//Текущий этап соединения - уничтожение
	connectionSetStage(con, CON_STAGE_DESTROYING);

	//Если соединение находится в очереди задач - удаляем из очереди
	//Если удалить не получилось (поскольку соединение в данный момен отбрабатывается рабочим потоком -> выходим)
	if(!jobDelete(con)) return RESULT_OK;

	//Уменьшение счетчика активных соединений, перемещение удаляемого соединения в конец массива активных соедниний
	srv->connections_count--;
	if(con->index < srv->connections_count){
		srv->connections[con->index] = srv->connections[srv->connections_count];
		srv->connections[con->index]->index = con->index;
		srv->connections[srv->connections_count] = con;
	}

	//DEBUG_MSG("connectionClosed. -> %d", con->fd);

	//Сброс удаляемого соединения до начатльных параметров
	connectionClear(con);
	con->index			= -1;
	con->fd				= -1;	//Дескриптор соединения
	con->stage			= CON_STAGE_NONE;
	con->server			= srv;	//Ссылка на структуру сервера
	con->http_code		= 200;	//HTTP статус обработки запроса (код ответа)
	con->job_stage		= JOB_STAGE_NONE;
	con->job_item		= NULL;

	return RESULT_OK;
}//END: connectionDelete



/*
 * Закрытие соединения
 */
result_e
connectionClose(connection_s * con){

	if(!con) RETURN_ERROR(RESULT_ERROR, "con is NULL");
	if(!con->server) FATAL_ERROR("!con->server");

	if(con->stage == CON_STAGE_NONE) RETURN_ERROR(RESULT_OK, "con->stage == CON_STAGE_NONE");

	//Если соединение уже закрыто или находится на стадии уничтожения -> удаляем
	//Если соединение не используется - просто выходим из функции
	if(con->stage >= CON_STAGE_CLOSED){
		if(con->index >= 0) return connectionDelete(con);
		return RESULT_OK;
	}

	//Соединение не может быть закрыто на текущей стадии
	if(con->stage < CON_STAGE_CLOSE) RETURN_ERROR(RESULT_ERROR, "con->stage < CON_STAGE_CLOSE");

	//Текущий этап соединения - закрытие сокета
	connectionSetStage(con, CON_STAGE_CLOSED);

	#ifdef XG_ERROR
	if(con->connection_error != CON_ERROR_NONE) DEBUG_MSG("connection FD=%d error: %s",con->fd, connectionErrorAsString(con->connection_error));
	#endif

	//Закрытие SSL соединения
	if(con->ssl){
		SSL_shutdown(con->ssl);
	}

	if(con->fd > -1){

		//Закрытие сокета
		socketClose(con->fd);

		//Удаление дескриптора сокета из Poll Engine
		fdEventDelete(con->server->fdevent, con->fd);	//Удаление событий из pollfds
		fdeventRemove(con->server->fdevent, con->fd);	//Удаление дескриптора из fds

	}

	//Удаление соединения
	if(con->index >= 0) return connectionDelete(con);

	return RESULT_OK;
}//END: connectionClose



/*
 * Принимает клиентское соединение
 */
connection_s * 
connectionAccept(server_s * srv){
	if(srv->connections_count >= server_max_connections) RETURN_ERROR(NULL,"srv->connections_count >= server_max_connections");	//Достигнут лимит на количество установленных соединений
	connection_s 	* con = NULL;
	socket_addr_s	addr;
	socklen_t		len = sizeof(addr);
	socket_t fd;
	int error_no;

	//Открытие соединения
	if((fd = accept(srv->listen_fd, (struct sockaddr *) &addr, &len)) == -1) return NULL; //RETURN_ERROR(NULL, "[%d] accept failed: %s", errno, strerror(errno));

	//Установка сокета в неблокируемое состояние + открытие на чтение / запись
	if(fcntl(fd, F_SETFL, O_NONBLOCK | O_RDWR)==-1){
		error_no = errno;
		socketClose(fd);
		RETURN_ERROR(NULL, "[%d] fcntl failed: %s", error_no, strerror(error_no));
	}

	DEBUG_MSG("Incomming connection: FD = %d", fd);

	//Получение структуры соединения
	if((con = connectionGet(srv))==NULL) RETURN_ERROR(NULL, "connectionGet() failed");

	con->fd = fd;
	memcpy(&(con->remote_addr), &addr, sizeof(socket_addr_s));

/*
	char * ipaddr_s = ipToString(&con->remote_addr, NULL);
	socket_addr_s	* addr_t = stringToIp(ipaddr_s, NULL);
	char * ipaddr2_s = ipToString(addr_t, NULL);
	printf("Incomming connection: %s (%s)\n",ipaddr_s,ipaddr2_s);
	printf("IP is equal = %s\n",(ipCompare(&con->remote_addr,addr_t) ? "true" : "false" ));
*/

	//Добавление дескриптора сокета в Poll Engine
	fdeventAdd(srv->fdevent, fd, connectionHandleFdEvent, con);

	//Добавляем событие на чтение из сокета для текущего соединения
	fdEventSet(srv->fdevent, fd, FDPOLL_READ);

	con->start_ts			= srv->current_ts;		//Unix время начала соединения
	con->http_code			= 200;					//HTTP статус обработки запроса (код ответа)
	con->stage 				= CON_STAGE_ACCEPTING;	//Инициализация соединения
	con->job_stage 			= JOB_STAGE_NONE;
	con->response.head		= bufferCreate(response_buffer_head_increment);
	con->response.content	= chunkqueueCreate();
	con->connection_error	= CON_ERROR_NONE;

	return con;
}//END: connectionAccept



/*
 * Обновляет подписчика на события poll engine в зависимости от состояния соединения
 */
static void
connectionFdEventUpdate(connection_s * con){

	switch(con->stage){
		case CON_STAGE_ACCEPTING:
		case CON_STAGE_HANDSTAKE:
		case CON_STAGE_READ:
			fdEventSet(con->server->fdevent, con->fd, FDPOLL_READ);
		break;
		case CON_STAGE_WRITE:
			fdEventSet(con->server->fdevent, con->fd, FDPOLL_WRITE);
		break;
		default:
			fdEventDelete(con->server->fdevent, con->fd);
		break;
	}

}//END: connectionFdEventUpdate



/*
 * Обработка соединения согласно его текущего этапа жизненного цикла
 */
result_e
connectionEngine(connection_s * con){

	server_s * srv = con->server;
	int ret;
	result_e result;
	connection_stage_e old_stage;
	char tmp_buf[4];

	//Обработка статуса соединения (пока обрабатывается)
	for(;;){

		old_stage = con->stage;

		//Обработка соединения согласно его текущего статуса
		switch(old_stage){

			//В текущий момент данное соединение не используется и с ним ничего сделать не получится
			case CON_STAGE_NONE:
				connectionFdEventUpdate(con);
				return RESULT_OK;
			break;


			//Принимаем соединение для обработки
			case CON_STAGE_ACCEPTING:
				//Чтение первого байта запроса клиента
				switch(socketReadPeek(con->fd, tmp_buf, 1, NULL)){
					//Первый байт запроса клиета прочитан
					case RESULT_OK:
						//Если SSL запрос: 0x80 - SSLv2 , 0x16 - SSLv3 / TLSv1
						if (tmp_buf[0] & 0x80 || tmp_buf[0] == 0x16){
							//Если SSL отключен - закрываем соединение
							if(!srv->config.use_ssl){
								connectionSetStage(con, CON_STAGE_CLOSE);
								con->connection_error = CON_ERROR_ACCEPT_REQUEST;
							}
							//Иначе - выполняем рукопожатие
							else{
								connectionSetStage(con, CON_STAGE_HANDSTAKE);
							}
						}else{
							//Если SSL включен - закрываем соединение
							if(srv->config.use_ssl){
								connectionSetStage(con, CON_STAGE_CLOSE);
								con->connection_error = CON_ERROR_ACCEPT_REQUEST;
							}
							//Иначе - принимаем соединение
							else{
								connectionSetStage(con, CON_STAGE_CONNECTED);
							}
						}
					break;
					//При чтении первого байта запроса клиета возникла ошибка сокета
					case RESULT_ERROR: 
						connectionSetStage(con, CON_STAGE_SOCKET_ERROR);
						con->connection_error = CON_ERROR_ACCEPT_SOCKET;
					break;
					//Первый байт запроса еще не готов для чтения, требуется повторить операцию
					case RESULT_AGAIN:
					default:
						//Если интервал ожитания первого байта из сокета более accepting_read_timeout секунд -> закрываем соединение
						if(srv->current_ts - con->start_ts > accepting_read_timeout){
							connectionSetStage(con, CON_STAGE_CLOSE);
							con->connection_error = CON_ERROR_ACCEPT_TIMEOUT;
						}else 
							return RESULT_OK;
					break;
				}

			break;


			//SSL Соединение было только что принято для обработки, ожидается "рукопожатие"
			case CON_STAGE_HANDSTAKE:

				//Создание SSL объекта, если он еще не был создан
				if(!con->ssl){
					//SSL соединение
					if ((con->ssl = SSL_new(srv->ctx)) == NULL){
						DEBUG_MSG("SSL error: %s", ERR_error_string(ERR_get_error(), NULL));
						connectionSetStage(con, CON_STAGE_ERROR);
						con->connection_error = CON_ERROR_HANDSTAKE_SSL_CREATE;
						return connectionClose(con);
					}
					con->renegotiations = 0;			//Счетчик количества "рукопожатий"
					SSL_set_app_data(con->ssl, con);	//Добавляем информацию о соединении в SSL
					SSL_set_accept_state(con->ssl);

					if(SSL_set_fd(con->ssl, con->fd) != 1){
						DEBUG_MSG("SSL error: %s", ERR_error_string(ERR_get_error(), NULL));
						connectionSetStage(con, CON_STAGE_SOCKET_ERROR);
						con->connection_error = CON_ERROR_HANDSTAKE_SSL_SET_FD;
						return connectionClose(con);
					}
				}//Создание SSL объекта, если он еще не был создан

				//Рукопожатие
				switch(sslDoHandshake(con->ssl)){
					case RESULT_ERROR: 
						connectionSetStage(con, CON_STAGE_SOCKET_ERROR);
						con->connection_error = CON_ERROR_HANDSTAKE_SOCKET;
					break;
					case RESULT_OK: 
						connectionSetStage(con, CON_STAGE_CONNECTED);
					break;
					case RESULT_AGAIN:
					default:
						if(srv->current_ts - con->start_ts > handstake_timeout){
							connectionSetStage(con, CON_STAGE_CLOSE);
							con->connection_error = CON_ERROR_HANDSTAKE_TIMEOUT;
						}else 
							return RESULT_OK;
					break;
				}
			break;


			//Соединение было только что принято для обработки
			case CON_STAGE_CONNECTED:
				//Создаем буфер приема данных от клиента, если такового еще нет
				if(!con->request.data) con->request.data = bufferCreate(request_buffer_increment);
				connectionSetStage(con, CON_STAGE_READ);
			break;


			//Получение данных от клиента
			case CON_STAGE_READ:

				//Поскольку на этой стадии помимо чтения данных запроса из сокета выполняется
				//их парсинг и анализ, и эта операция достаточно ресурсоемкая,
				//то она однозначно должна выполняться в рабочем потоке
				jobAdd(con);
				return RESULT_OK;

			break;


			//Обработка запроса и отправка ответа сервера
			case CON_STAGE_WORKING:
				connectionFdEventUpdate(con);
				jobAdd(con);
				return RESULT_OK;
			break;


			//Подготовка отправки ответа клиенту
			case CON_STAGE_BEFORE_WRITE:
				bufferSeekBegin(con->response.head);
				chunkqueueSetHeaderBuffer(con->response.content, con->response.head, false);
				chunkqueueReset(con->response.content);
				connectionSetStage(con, CON_STAGE_WRITE);
				connectionFdEventUpdate(con);
			//break;

			//Отправка ответа клиенту
			case CON_STAGE_WRITE:

				//Если есть свободные потоки - добавляем соединение в очередь заданий
				//Если же свободных потоков нет - пишем в сокет из основного потока
				if(srv->workers->threads_idle > 0){
					jobAdd(con);
					return RESULT_OK;
				}

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
						connectionSetStage(con, CON_STAGE_SOCKET_ERROR);
						con->connection_error = CON_ERROR_WRITE_SOCKET;
					break;
					//Соединение разорвано
					case RESULT_CONRESET:
						connectionSetStage(con, CON_STAGE_CLOSE);
						con->connection_error = CON_ERROR_DISCONNECT;
					break;
					//Данных больше нет - все данные успешно отправлены
					case RESULT_EOF:
					case RESULT_COMPLETE:
						connectionSetStage(con, CON_STAGE_COMPLETE);
					break;
					//Повторить и прочее
					case RESULT_AGAIN:
					default:
						return RESULT_OK;
					break;
				}

			break;


			//Запрос был получен, успешно обработан, завершающая стадия обработки запроса
			case CON_STAGE_COMPLETE:
				connectionFdEventUpdate(con);
				connectionSetStage(con, CON_STAGE_CLOSE);
			break;


			//Возникла ошибка при работе с сокетом в процессе соединения (установка соединения, чтение, запись и т.д.)
			case CON_STAGE_SOCKET_ERROR:
			//Возникла ошибка при работе в процессе соединения (не связанная с сокетом)
			case CON_STAGE_ERROR:

				//Если SSL соединение
				if(con->ssl){
					ERR_clear_error();
					//Попытка закрытия SSL сессии
					switch((ret = SSL_shutdown(con->ssl))){

						//SSL сессия закрыта
						case 1: break;

						//Алерт close notify был отправлен клиенту, но ответный close notify не был получен
						case 0:
							ERR_clear_error();
							//Поскольку предполагается закрытие нижележащего транспортного канала, то при получении результата, отличного от -1 можно закрывать канал
							if(SSL_shutdown(con->ssl) != -1) break;

						//Ошибка 
						default:
							//Обработка ошибки
							switch(SSL_get_error(con->ssl, ret)){
								case SSL_ERROR_WANT_WRITE:
								case SSL_ERROR_WANT_READ:
									SSL_shutdown(con->ssl);
								break;
								case SSL_ERROR_SYSCALL:
								default:
									CLEAR_SSL_ERRORS;
								break;
							}
					}//Попытка закрытия SSL сессии

				}//Если SSL соединение

				connectionSetStage(con, CON_STAGE_CLOSE);

				//Закрытие соединения на запись
				if (shutdown(con->fd, SHUT_WR) == 0){
					con->close_timeout_ts = srv->current_ts;
				}else{
					return connectionClose(con);
				}

			break;


			//Соединение находится на стадии закрытия и должно быть закрыто
			case CON_STAGE_CLOSE:
				return connectionClose(con);
			break;


			//Соединение закрыто, требуется его удалить
			case CON_STAGE_DESTROYING:
			case CON_STAGE_CLOSED:
				return connectionDelete(con);
			break;


			//Неизвестный статус соединения ???
			default:
				con->connection_error = CON_ERROR_UNDEFINED_STAGE;
				RETURN_ERROR(RESULT_ERROR, "con->stage [%d] is UNDEFINED",(int)(con->stage));
			break;

		}//Обработка соединения согласно его текущего статуса


		//Если статус соединения не изменился, считаем что - операция выполнена
		if(old_stage == con->stage) break;
		if(old_stage != con->stage) con->read_idle_ts = 0;	//Если статус соединения изменился, сбрасываем таймер простоя

	}//Обработка статуса соединения (пока обрабатывается)

	return RESULT_OK;
}//END: connectionEngine





/*
 * Чтение данных от клиента
 */
result_e
connectionHandleRead(connection_s * con){

	int toread = 0, n;
	buffer_s * buf = con->request.data;

	if(ioctl(con->fd, FIONREAD, &toread) == -1) return RESULT_ERROR;
	if(toread > 0) bufferIncrease(buf, min(request_buffer_increment, toread+1));

	//Чтение данных в буфер
	n = read(con->fd, bufferGetPtr(buf), bufferGetAllowedSize(buf)-1);

	if(n > 0){
		buf->index += n;
		buf->count = buf->index;
		buf->buffer[buf->index] = '\0';
	}

	if (n < 0){
		switch(errno){
			//Повторить
			case EAGAIN:
			case EINTR:
				return RESULT_AGAIN;
			//Соединение разорвано клиентом
			case ECONNRESET:
				return RESULT_CONRESET;
			//Все остальное - ошибка сокета
			default: 
				return RESULT_ERROR;
		}
	}
	else
	if (n == 0){
		return RESULT_EOF;
	}

	return connectionPrepareRequest(con);
}//END: connectionHandleRead





/*
 * Чтение SSL данных от клиента
 */
result_e
connectionHandleReadSSL(connection_s * con){

	ERR_clear_error();
	buffer_s * buf = con->request.data;
	int toread, n, error;

	do{
		toread = SSL_pending(con->ssl);
		if(toread > 0) bufferIncrease(buf, min(request_buffer_increment, toread+1));

		//Чтение данных в буфер
		n = SSL_read(con->ssl, bufferGetPtr(buf), bufferGetAllowedSize(buf)-1);

		if(n > 0){
			buf->index += n;
			buf->count = buf->index;
			buf->buffer[buf->index] = '\0';
		}

	}while(/*n >*/ 0);


	//Ошибка при получении данных
	if (n < 0) {
		error = errno;
		switch(SSL_get_error(con->ssl, n)){
			case SSL_ERROR_NONE:
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE: 
				return RESULT_AGAIN;
			case SSL_ERROR_ZERO_RETURN: 
				return RESULT_EOF;
			case SSL_ERROR_SYSCALL:
				CLEAR_SSL_ERRORS;
				switch (error) {
					//Повторить
					case EAGAIN:
					case EINTR:
						return RESULT_AGAIN;
					//Соединение разорвано
					case EPIPE:
					case ECONNRESET: 
						return RESULT_CONRESET;
					default: 
						return RESULT_ERROR;
				}
			default:
				return RESULT_ERROR;
		}
	} else if (n == 0){
		return RESULT_EOF;
	}

	return connectionPrepareRequest(con);
}//END: connectionHandleReadSSL




/*
 * Парсинг и обработка данных запроса
 */
result_e
connectionPrepareRequest(connection_s * con){
	request_parser_s * parser = &(con->request.parser);
	buffer_s * buf = con->request.data;
	const char * line;
	const char * ptr;
	int code;

	//В настоящий момент находимся в заголовках запроса
	if(parser->in == IN_HEADERS){

		//Присваиваем ptr позицию от начала строки заголовка
		line = &buf->buffer[parser->line_n];
		//Просматриваем доступные строки
		while((ptr = strchr(line, '\n')) != NULL){
			parser->line_len = ptr - line;
			if(*(ptr-1)=='\r') parser->line_len--;

			ptr++;	//Пропускаем [\n]

			//Найдена пустая строка - конец заголовков
			if(parser->line_len == 0){
				parser->in = IN_BODY;
				parser->body_n = ptr - buf->buffer;
				//Обработка полученных заголовков
				if((code = requestHeadersToVariables(con)) != 0){
					con->http_code = code;
				}else{
					//Если POST запрос и размер контента больше 0 - увеличиваем размер буфера на content_length
					if(con->request.request_method == HTTP_POST && con->request.content_length > 0 && con->http_code == 200){
						bufferIncrease(buf, con->request.content_length);
					}
				}
				break;
			}

			//Обработка первой строки запроса
			if(parser->line_no == 0){
				//Если первая строка запроса обработана с ошибкой - дальнейший парсинг не имеет смысла
				if((code = requestParseFirstLine(con, line, parser->line_len)) != 0){
					con->http_code = code;
					break;
				}
				//Поиск алиаса для запрошенного URI документа
				if(XG_ALIASES){
					kv_s * alias_kv = kvSearch(XG_ALIASES, con->request.uri.path.ptr, con->request.uri.path.len);
					if(alias_kv){
						if(alias_kv->type == KV_STRING){
							const char * alias = alias_kv->value.v_string.ptr;
							uint32_t alias_len = alias_kv->value.v_string.len;
							if(alias && *alias && alias_len > 0){
								//Внутренний маршрут
								if(*alias == '/'){
									//Заменяем запрошенный маршрут реальным маршрутом
									mStringClear(&con->request.uri.path);
									con->request.uri.path.ptr = stringCloneN(alias, alias_len, &con->request.uri.path.len);
								}
								//Редирект на внешний URL
								else{
									responseHttpLocation(con, alias, false);
									return RESULT_COMPLETE;
								}
							}
						}else 
						if(alias_kv->type == KV_INT){
							con->http_code = (int)alias_kv->value.v_int;
							break;
						}
					}//alias_kv
				}//XG_ALIASES
			}
			//Обработка строки заголовка
			else{
				//Если это первый заголовок, инициализируем список заголовков
				if(!con->request.headers) con->request.headers = kvNewRoot();
				if((code = requestParseHeaderLine(con, line, parser->line_len)) != 0){
					con->http_code = code;
					break;
				}
			}

			parser->line_n = ptr - buf->buffer;
			line = ptr;
			parser->line_no++;

		}//Просматриваем доступные строки

		//Если мы остановились на конце буфера - возвращаемся и ожидаем следующей партии данных от клиента
		if(!ptr) return RESULT_OK;

	}//В настоящий момент находимся в заголовках запроса

	//Если возникла ошибка
	if(con->http_code != 200){
		connectionSetStage(con, CON_STAGE_WORKING);
		return RESULT_COMPLETE;
	}

	//В настоящий момент находимся в теле запроса
	if(parser->in == IN_BODY){

		#if 0
		buffer_s * buf = bufferCreate(0);
		kvEchoHeaders(buf, con->request.headers);
		bufferPrint(buf);
		bufferFree(buf);
		#endif

		//Если не POST запрос или при обработке заголовков возникла ошибка
		if(con->request.request_method != HTTP_POST || con->http_code != 200){
			connectionSetStage(con, CON_STAGE_WORKING);
			return RESULT_COMPLETE;
		}

		//Получено нужное количество байт контента
		if((parser->body_len = buf->count - parser->body_n) == con->request.content_length){
			connectionSetStage(con, CON_STAGE_WORKING);
			return RESULT_COMPLETE;
		}

		//Размер полученных данных больше, чем указано в Content-Length
		if(parser->body_len > con->request.content_length){
			con->http_code = 413;	//413 Request Entity Too Large
			connectionSetStage(con, CON_STAGE_WORKING);
			return RESULT_COMPLETE;
		}

	}//В настоящий момент находимся в теле запроса


	return RESULT_OK;
}//END: connectionPrepareRequest





/*
 * Отправка данных клиенту
 */
result_e
connectionHandleWrite(connection_s * con){

	int n;
	result_e result;
	const char * ptr = NULL;
	uint32_t	len = 0;

	do{
		result = chunkqueueRead(con->response.content, &ptr, &len);
		if(result != RESULT_OK) return result;
		n = write(con->fd, ptr, len);
		if(n > 0) chunkqueueCommit(con->response.content, n);
	}while(n > 0);


	if (n < 0){
		switch(errno){
			//Повторить
			case EAGAIN:
			case EINTR:
				return RESULT_AGAIN;
			//Соединение разорвано клиентом
			case ECONNRESET:
				return RESULT_CONRESET;
			//Все остальное - ошибка сокета
			default: 
				return RESULT_ERROR;
		}
	}
	else
	if (n == 0){
		return RESULT_EOF;
	}

	return RESULT_OK;
}//END: connectionHandleWrite



/*
 * Отправка данных клиенту
 */
result_e
connectionHandleWriteSSL(connection_s * con){

	ERR_clear_error();
	int n, error;
	result_e result;
	const char * ptr = NULL;
	uint32_t	len = 0;

	do{
		result = chunkqueueRead(con->response.content, &ptr, &len);
		if(result != RESULT_OK) return result;
		n = SSL_write(con->ssl, ptr, len);
		if(n > 0) chunkqueueCommit(con->response.content, n);
	}while(n > 0);

	//Ошибка при получении данных
	if (n < 0) {
		error = errno;
		switch(SSL_get_error(con->ssl, n)){
			case SSL_ERROR_NONE:
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE: 
				return RESULT_AGAIN;
			case SSL_ERROR_SYSCALL:
				CLEAR_SSL_ERRORS;
				switch (error) {
					//Повторить
					case EAGAIN:
					case EINTR:
						return RESULT_AGAIN;
					//Соединение разорвано
					case EPIPE:
					case ECONNRESET: 
						return RESULT_CONRESET;
					default: 
						return RESULT_ERROR;
				}
			default:
				return RESULT_ERROR;
		}
	} else if (n == 0){
		return RESULT_EOF;
	}

	return RESULT_OK;
}//END: connectionHandleWrite





/*
 * Функция-обработчик события Poll Engine для клиентского сокета
 */
result_e
connectionHandleFdEvent(server_s * srv, int revents, void * data){

	if(!data) RETURN_ERROR(RESULT_ERROR, "connectionHandleFdEvent: data is NULL");
	connection_s * con = (connection_s *)data;

	//Если получено событие, отличное от чтения/записи
	if (BIT_ISUNSET(revents, FDPOLL_IN) && BIT_ISUNSET(revents, FDPOLL_OUT)){
		//Разрыв соединения
		if (BIT_ISSET(revents, FDPOLL_HUP)){
			connectionSetStage(con, CON_STAGE_CLOSE);
			con->connection_error = CON_ERROR_DISCONNECT;
		}else
		//Ошибка сокета
		if (BIT_ISSET(revents, FDPOLL_ERR)){
			connectionSetStage(con, CON_STAGE_SOCKET_ERROR);
			con->connection_error = CON_ERROR_FDEVENT_SOCKET;
		}
		//Прочие события
		else{
			connectionSetStage(con, CON_STAGE_ERROR);
			con->connection_error = CON_ERROR_FDEVENT_UNDEFINED;
		}
		return RESULT_OK;
	}//Если получено событие, отличное от чтения/записи

	//Если соединение в настоящий момент не обрабатывается - обрабатываем
	//Если же соединение обрабатывается рабочим потоком, то оно будет добавлено 
	//в список заданий для основного потока и connectionEngine() будет выполнен чуть позже
	if(con->job_stage == JOB_STAGE_NONE) connectionEngine(con);

	return RESULT_OK;
}//END: connectionHandleFdEvent













































