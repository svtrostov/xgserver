/***********************************************************************
 * XG SERVER
 * core/server.c
 * Функции WEB сервера
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include "server.h"
#include "globals.h"
#include "event.h"


//Буфер-мусорка
static char trash_buffer[1024 * 16];



/***********************************************************************
 * Работа с сервером
 **********************************************************************/ 


/*
 * Применяет опции конфигурации из webserver.conf к серверу
 */
void
serverSetConfig(server_s * srv){
	srv->config.host					= stringClone(configGetString("/webserver/host","*"),NULL);					//Прослушиваемый Хост
	srv->config.port					= (int)configGetInt("/webserver/port", 8888);									//Прослушиваемый порт (значения от 80 до 65000)
	if(srv->config.port < 80 ||srv->config.port > 65000) FATAL_ERROR("[/webserver/port] = [%u], out of range, value should be between 80 and 65000\n", srv->config.port);
	srv->config.max_head_size			= max(0,(int)configGetInt("/webserver/max_head_size", 64*1024));					//Максимальный размер HTTP заголовков, принимаемых сервером, в байтах (по-умолчанию, 65536 байт = 64кб)
	srv->config.max_post_size			= max(0,(int)configGetInt("/webserver/max_post_size", 1024*1024));				//Максимальный размер POST данных, принимаемых сервером, в байтах (по-умолчанию, 1048576 байт = 1Мб)
	srv->config.max_upload_size			= max(0,(int)configGetInt("/webserver/max_upload_size", 512*1024));				//Максимальный размер загружаемого файла, принимаемого сервером, в байтах (по-умолчанию, 524288 байт = 500кб)
	srv->config.max_read_idle			= max(0,min(30,(int)configGetInt("/webserver/max_read_idle", 2)));				//Маскимальное время ожидания данных от клиента (в секундах)
	srv->config.max_request_time		= max(0,min(86400,(int)configGetInt("/webserver/max_request_time", 10)));		//Маскимальное время получения запроса от клиента (в секундах)
	srv->config.public_html.ptr		= fileRealpath(configRequireString("/webserver/public_html"), &srv->config.public_html.len);		//Папка, содержащая статичный контент (html, js, css, изобажения, видео и прочие файлы, которые не требуют обработки)
	if(!srv->config.public_html.ptr) FATAL_ERROR("[/webserver/public_html] path not found\n");
	if(!dirExists(srv->config.public_html.ptr)) FATAL_ERROR("Directory [%s] not found\n",srv->config.public_html.ptr);

	srv->config.private_html.ptr		= fileRealpath(configRequireString("/webserver/private_html"), &srv->config.private_html.len);		//Папка, содержащая закрытый статичный контент (html шаблоны, приватные изображения и т.д.)
	if(!srv->config.private_html.ptr) FATAL_ERROR("[/webserver/private_html] path not found\n");
	if(!dirExists(srv->config.private_html.ptr)) FATAL_ERROR("Directory [%s] not found\n",srv->config.private_html.ptr);

	srv->config.private_key_file		= stringClone(configRequireString("/webserver/private_key_file"),NULL);		//Путь к файлу закрытого ключа сервера в формате PEM "key.pem"
	srv->config.private_key_password	= stringClone(configRequireString("/webserver/private_key_password"),NULL);	//Пароль закрытого ключа сервера "key.pem"
	srv->config.certificate_file		= stringClone(configRequireString("/webserver/certificate_file"),NULL);		//Путь к файлу сертификата сервера  в формате PEM "cert.pem"
	srv->config.dh512_file				= stringClone(configRequireString("/webserver/dh512_file"),NULL);			//Путь к файлам DH параметров: openssl dhparam -out dh512.pem 512
	srv->config.dh1024_file				= stringClone(configRequireString("/webserver/dh1024_file"),NULL);			//Путь к файлам DH параметров: openssl dhparam -out dh1024.pem 1024
	srv->config.dh2048_file				= stringClone(configRequireString("/webserver/dh2048_file"),NULL);			//Путь к файлам DH параметров: openssl dhparam -out dh2048.pem 2048
	srv->config.use_ssl					= configGetBool("/webserver/use_ssl", true);								//Использовать SSL
	srv->config.mimetypes				= kvGetRequireType(XG_CONFIG, "/webserver/mimetypes", KV_OBJECT);			//MIME типы файлов
	srv->config.default_mimetype		= kvGetRequireStringS(srv->config.mimetypes, "default");					//MIME тип по-умолчанию
	srv->config.directory_index.ptr		= stringClone(configGetString("/webserver/directory_index","index.php"), &srv->config.directory_index.len);	//Название файла по-умолчанию, если в URI запроса указана директория (последний символ URI = "/")
	srv->config.worker_threads			= max(0,min(64,(int)configGetInt("/webserver/worker_threads", 0)));
}//END: serverSetConfig



/*
 * Инициализация прослушивающего сокета сервера
 */
void
serverInitListener(server_s * srv){

	size_t addr_len;

	if(srv->config.host[0] == '[' || strchr(srv->config.host, ':') != NULL) srv->socket_type = SOCKTYPE_IPV6;

	//Открытие сокета
	switch(srv->socket_type){

		//IPv6
		case SOCKTYPE_IPV6:
			srv->addr.plain.sa_family = AF_INET6;
			if ((srv->listen_fd = socket(srv->addr.plain.sa_family, SOCK_STREAM, IPPROTO_TCP)) == -1){
				FATAL_ERROR("IPv6 socket failed: %s", strerror(errno));
			}

			memset(&srv->addr, 0, sizeof(struct sockaddr_in6));
			srv->addr.ipv6.sin6_family = AF_INET6;
			//"[]" или "[*]" - любой IP сервера на этом порте
			if (srv->config.host[1] == '*' || srv->config.host[1] == ']') {
				srv->addr.ipv6.sin6_addr = in6addr_any;
			} else {
				struct addrinfo hints, *res;
				int r;
				memset(&hints, 0, sizeof(hints));
				hints.ai_family   = AF_INET6;
				hints.ai_socktype = SOCK_STREAM;
				hints.ai_protocol = IPPROTO_TCP;
				if ((r = getaddrinfo(srv->config.host, NULL, &hints, &res)) != 0){
					FATAL_ERROR("getaddrinfo failed for host [%s]: %s", srv->config.host, gai_strerror(r));
				}
				memcpy(&(srv->addr), res->ai_addr, res->ai_addrlen);
				freeaddrinfo(res);
			}
			srv->addr.ipv6.sin6_port = htons(srv->config.port);
			addr_len = sizeof(struct sockaddr_in6);

		break;

		case SOCKTYPE_IPV4:
		default:
			srv->addr.plain.sa_family = AF_INET;
			if ((srv->listen_fd = socket(srv->addr.plain.sa_family, SOCK_STREAM, IPPROTO_TCP)) == -1){
				FATAL_ERROR("IPv4 socket failed: %s", strerror(errno));
			}

			memset(&srv->addr, 0, sizeof(struct sockaddr_in));
			srv->addr.ipv4.sin_family = AF_INET;
			//"" или "*" - любой IP сервера на этом порте
			if (srv->config.host[0] == '*' || srv->config.host[0] == '\0') {
				srv->addr.ipv4.sin_addr.s_addr = htonl(INADDR_ANY);
			}else{
				struct hostent *he;
				if ((he = gethostbyname(srv->config.host))== NULL) FATAL_ERROR("gethostbyname failed for host [%s]: %d", srv->config.host, h_errno);
				if (he->h_addrtype != AF_INET) FATAL_ERROR("addr-type != AF_INET: %d", he->h_addrtype);
				if (he->h_length != sizeof(struct in_addr)) FATAL_ERROR("addr-length != sizeof(in_addr): %u", he->h_length);
				memcpy(&(srv->addr.ipv4.sin_addr.s_addr), he->h_addr_list[0], he->h_length);
			}
			srv->addr.ipv4.sin_port = htons(srv->config.port);
			addr_len = sizeof(struct sockaddr_in);

		break;
	}//Открытие сокета


	int val = 1;
	if (setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0){
		FATAL_ERROR("socketsockopt(SO_REUSEADDR) failed: %s", strerror(errno));
	}

	if(socketSetNonblockState(srv->listen_fd, true) == -1){
		FATAL_ERROR("socketSetNonblockState() failed: %s", strerror(errno));
	}

	if (bind(srv->listen_fd, (struct sockaddr *) &(srv->addr), addr_len) != 0){
		FATAL_ERROR("bind to socket failed: %s", strerror(errno));
	}

	if (listen(srv->listen_fd, SOMAXCONN) == -1) {
		FATAL_ERROR("listen failed: %s", strerror(errno));
	}

}//END: serverInitListener



/*
 * Функция обработки событий для серверного сокета в Poll Engine
 */
static result_e 
serverHandleFdEvent(server_s * srv, int revents, void * data){
	connection_s * con;
	int loops = 150;

	//Неизвестное событие для серверного сокета
	if (BIT_ISUNSET(revents, FDPOLL_IN)) RETURN_ERROR(RESULT_ERROR, "strange event [%d] for server socket [%d]", revents, srv->listen_fd);

	//Принимаем loops новых соединений, ограничение loops нужно,
	//чтобы была возможность обработать уже установленные соединения
	for (; loops > 0 && NULL != (con = connectionAccept(srv)); loops--) connectionEngine(con);

	return RESULT_OK;
}//END: serverHandleFdEvent



/*
 * Функция обработки событий для прерывающего сокета в Poll Engine
 */
static result_e 
serverHandlePipeEvent(server_s * srv, int revents, void * data){
	read(srv->pipe[0], trash_buffer,sizeof(trash_buffer));
	return RESULT_OK;
}//END: serverHandlePipeEvent



/*
 * Инициализация сервера
 */
void
serverInit(void){

	server_s * srv = mNewZ(sizeof(server_s));


	//Создание структур клиентских соединений
	connectionsCreate(srv);

	//Настройки сервера
	serverSetConfig(srv);


	//Генерация события (описание в core/event.h)
	fireEvent(EVENT_SERVER_INIT, srv);


	//Инициализация Poll engine
	fdeventNew(srv);

	//Инициализация SSL
	if(srv->config.use_ssl) sslInit(srv);

	//Инициализация прослушивающего сокета сервера
	serverInitListener(srv);

	//Инициализация списка заданий для рабочих потоков
	if(joblistCreate(srv)!=RESULT_OK) FATAL_ERROR("Init joblist fail");

	//Инициализация списка заданий для основного потока
	if(jobmainCreate(srv)!=RESULT_OK) FATAL_ERROR("Init jobmain fail");

	//Инициализация рабочих потоков сервера
	if(threadPoolCreate(srv, (!srv->config.worker_threads ? (size_t)sysconf(_SC_NPROCESSORS_ONLN) : (size_t)srv->config.worker_threads))!=RESULT_OK) FATAL_ERROR("Init workers threads fail");


	//Регистрация сокета сервера в обработчике событий Poll Engine
	fdeventAdd(srv->fdevent, srv->listen_fd, serverHandleFdEvent, srv);
	fdEventSet(srv->fdevent, srv->listen_fd, FDPOLL_IN);

#ifdef USE_POLL

	if(pipe(srv->pipe)==-1)  FATAL_ERROR("pipe fail: %s", strerror(errno));
	DEBUG_MSG("srv->pipe[0] (read) = %d", srv->pipe[0]);
	DEBUG_MSG("srv->pipe[1] (write) = %d", srv->pipe[1]);

	//Регистрация сокета сервера в обработчике событий Poll Engine
	fdeventAdd(srv->fdevent, srv->pipe[0], serverHandlePipeEvent, srv);
	fdEventSet(srv->fdevent, srv->pipe[0], FDPOLL_IN);

#endif

	int n;
	int revents;
	int poll_index;
	fdevent_handler handler;
	void * data;
	result_e result;
	socket_t fd;
	time_t old_ts;
	connection_s * con;

	XG_STATUS = XGS_WORKING;
	srv->current_ts = time(NULL);
	old_ts = srv->current_ts;

	#ifdef XG_MEMSTAT
	sleepSeconds(2);
	mStatStart();
	#endif


	#ifdef XG_MEM_USE_CACHE
	mCacheOn();		//Включение режима кеширования
	#endif

	//Генерация события (описание в core/event.h)
	fireEvent(EVENT_SERVER_START, srv);

	//Основный цикл приема соединений
	while(XG_STATUS == XGS_WORKING){

		//Текущее время
		srv->current_ts = time(NULL);

		//Обработка списка заданий jobmain
		while((con=jobmainGet(srv->jobmain))!=NULL) connectionEngine(con);

		//Получение новых событий, n - количество новых событий
#ifdef USE_EPOLL
		n = fdeventPoll(srv->fdevent, 100);
#endif
#ifdef USE_POLL
		n = poll(srv->fdevent->pollfds, srv->fdevent->pollfds_count, 100);
#endif

/*
		if(n>0){
			DEBUG_MSG("%u: poll events found: %d",(uint32_t)srv->current_ts,n);
		}
*/

		poll_index = -1;
		//Обработка событий poll engine
		while(n-- > 0){

			//Получаем индекс массива pollfds, для дескриптора сокета которого есть события или -1, если ничего не найдено
			if((poll_index = fdEventGetNextIndex(srv->fdevent, poll_index)) == -1) break;

#ifdef USE_EPOLL
			fd		= srv->fdevent->epollfds[poll_index].data.fd;
			revents = srv->fdevent->epollfds[poll_index].events;
#endif

#ifdef USE_POLL
			fd		= srv->fdevent->pollfds[poll_index].fd;
			revents = srv->fdevent->pollfds[poll_index].revents;
#endif

			//DEBUG_MSG("\t event fd = %d", fd);

			if (srv->fdevent->fds[fd] == NULL || srv->fdevent->fds[fd]->fd != fd) FATAL_ERROR("srv->fdevent->fds[fd] == NULL || srv->fdevent->fds[fd]->fd != fd");
			handler = srv->fdevent->fds[fd]->handler;
			data = srv->fdevent->fds[fd]->data;

			switch (result = (*handler)(srv, revents, data)){
				case RESULT_ERROR: FATAL_ERROR("HANDLER RETURNED RESULT_ERROR");
				default: break;
			}

		}//Обработка событий poll engine


		//Обработка списка заданий jobmain
		while((con=jobmainGet(srv->jobmain))!=NULL) connectionEngine(con);


		//Если текущее время изменилось (в секундах, разумеется)
		if(old_ts != srv->current_ts){

			//Добавление внутреннего задания на удаление сессий с истекшим сроком действия
			if(srv->current_ts % 60 == 0) jobinternalAdd(JOB_INTERNAL_SESSION_CLEANER, NULL, NULL);

			if(srv->current_ts % 5 == 0){

				#ifdef XG_MEMSTAT
				mStatPrint();
				#endif

				#ifdef XG_CONSTAT
				printf("\n----------------------------------\n");
				printf("Server thr idle: %u\n", (uint32_t)srv->workers->threads_idle);
				connectionsPrint(srv);
				#endif

			}

			old_ts = srv->current_ts;

			//Просмотр активных соединений
			for(n = srv->connections_count-1; n >= 0; n--){
				con = srv->connections[n];
				if(!con || con->stage == CON_STAGE_NONE) continue;
				if(con->fd < 0 || con->stage >= CON_STAGE_CLOSED){
					connectionDelete(con);
					continue;
				}
				//Проверка таймаутов
				if(	(con->read_idle_ts > 0 && srv->current_ts - con->read_idle_ts > srv->config.max_read_idle && con->stage == CON_STAGE_READ) ||	//Превышен интервал ожидания данных между двумя socket read операциями
					(srv->current_ts - con->start_ts > srv->config.max_request_time && (con->stage >= CON_STAGE_ACCEPTING && con->stage <= CON_STAGE_READ))	//Превышен лимит времени на получение запроса от клиента: CON_STAGE_ACCEPTING, CON_STAGE_HANDSTAKE, CON_STAGE_CONNECTED, CON_STAGE_READ
				){
					con->http_code = 408;	// 408 Request Timeout - время ожидания сервером передачи от клиента истекло
					if(con->request.data){
						con->stage = (con->request.data->count > 0 ? CON_STAGE_WORKING : CON_STAGE_CLOSE);
					}else{
						con->stage = CON_STAGE_CLOSE;
					}
					con->connection_error = CON_ERROR_TIMEOUT;
					connectionEngine(con);
				}

			}//Просмотр активных соединений

		}//Если текущее время изменилось (в секундах, разумеется)


	}//Основный цикл приема соединений


	//Генерация события (описание в core/event.h)
	fireEvent(EVENT_SERVER_STOP, srv);


	#ifdef XG_MEM_USE_CACHE
	mCacheOff();		//Выключение режима кеширования
	#endif

	#ifdef XG_MEMSTAT
	mStatStop();
	#endif

	DEBUG_MSG("threadPoolFree()...");
	threadPoolFree(srv->workers);
	DEBUG_MSG("connectionsFree()...");
	connectionsFree(srv);
	DEBUG_MSG("Closing pipe FDs...");
	if(srv->pipe[0] > -1) close(srv->pipe[0]);
	if(srv->pipe[1] > -1) close(srv->pipe[1]);
	DEBUG_MSG("jobmainFree()...");
	jobmainFree(srv->jobmain);
	DEBUG_MSG("joblistFree()...");
	joblistFree(srv->joblist);
	DEBUG_MSG("fdeventFree()...");
	fdeventFree(srv->fdevent);
	DEBUG_MSG("socketClose()...");
	socketClose(srv->listen_fd);
	DEBUG_MSG("sessionCacheSaveAll()...");
	sessionCacheSaveAll();
	DEBUG_MSG("SSL_CTX_free()...");
	SSL_CTX_free(srv->ctx);
	DEBUG_MSG("sslCleanup()...");
	sslCleanup();
	DEBUG_MSG("jobinternalThreadFree()...");
	jobinternalThreadFree();

	DEBUG_MSG("XG Server stopped");

	return;
}//END: serverInit




