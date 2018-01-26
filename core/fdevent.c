/***********************************************************************
 * XG SERVER
 * core/fdevent.c
 * Функции Poll engine
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/    


#include "server.h"


static fd_s * _fds_idle_list = NULL;

/*
 * Добавляет новый/существующмй элемент в IDLE список
 */
static void
_fdsToIdle(fd_s * item){
	if(!item) item = (fd_s *)mNew(sizeof(fd_s));
	item->next = _fds_idle_list;
	_fds_idle_list = item;
}//END: _fdsToIdle



/*
 * Получает элемент из IDLE списка или создает новый, если IDLE список пуст
 */
static fd_s *
_fdsFromIdle(void){
	fd_s * item = NULL;
	if(_fds_idle_list){
		item = _fds_idle_list;
		_fds_idle_list = item->next;
		memset(item,'\0',sizeof(fd_s));
	}else{
		item = (fd_s *)mNewZ(sizeof(fd_s));
	}
	return item;
}//END: _fdsFromIdle



/***********************************************************************
 * Функции
 **********************************************************************/ 


/*
 * Создание элемента fd_s
 */
fd_s *
fdNew(void){
	fd_s * fdn = _fdsFromIdle();
	fdn->fd = -1;
	return fdn;
}



/*
 * Освобождение элемента fd_s
 */
inline void 
fdFree(fd_s *fdn){
	_fdsToIdle(fdn);
}//END: fdeventFree



/*
 * Создание структуры FD events
 */
fdevent_s *
fdeventNew(server_s * srv){
	srv->fdevent = mNewZ(sizeof(fdevent_s));
	srv->fdevent->server = srv;
	srv->fdevent->fds = mNewZ(server_max_fds * sizeof(fd_s *));
#ifdef USE_EPOLL
	if( (srv->fdevent->epoll_fd = epoll_create(server_max_fds)) == -1) FATAL_ERROR("epoll_create failed: %s",  strerror(errno));
	srv->fdevent->epollfds = malloc(server_max_fds * sizeof(struct epoll_event));
#endif
#ifdef USE_POLL
	srv->fdevent->pollfds = mNewZ(server_max_fds * sizeof(struct pollfd));
#endif
	int i;
	for(i=0;i<server_max_fds;i++) _fdsToIdle(NULL);
	return srv->fdevent;
}//END: fdeventNew



/*
 * Освобождение структуры FD events
 */
void 
fdeventFree(fdevent_s * ev){
	if (!ev) return;
	size_t i;
	for (i = 0; i < server_max_fds; i++) {
		if (ev->fds[i]) fdFree(ev->fds[i]);
	}
	mFree(ev->fds);
#ifdef USE_EPOLL
	close(ev->epoll_fd);
	mFree(ev->epollfds);
#endif
#ifdef USE_POLL
	mFree(ev->pollfds);
#endif
	mFree(ev);
}//END: fdeventFree



/*
 * Добавление сокета в Poll engine
 */
bool 
fdeventAdd(fdevent_s * ev, socket_t fd, fdevent_handler handler, void * data){
	if(fd < 0) return false;
	if(server_max_fds < fd) return false;
	fd_s * fdn 			= fdNew();
	fdn->handler		= handler;
	fdn->fd				= fd;
	fdn->data			= data;
	fdn->events			= 0;
	fdn->poll_index		= -1;
	ev->fds[fd]			= fdn;
	return true;
}//END: fdeventAdd



/*
 * Удаление сокета из Poll engine
 */
bool
fdeventRemove(fdevent_s * ev, socket_t fd) {
	if (!ev) return false;
	fd_s * fdn = ev->fds[fd];
	ev->fds[fd] = NULL;
	fdFree(fdn);
	return true;
}//END: fdeventRemove




/*
 * Получение событий сокетов
 */
int 
fdeventPoll(fdevent_s * ev, int timeout_ms){
#ifdef USE_EPOLL
	return epoll_wait(ev->epoll_fd, ev->epollfds, server_max_fds, timeout_ms);
#endif
#ifdef USE_POLL
	return poll(ev->pollfds, ev->pollfds_count, timeout_ms);
#endif
	return -1;
}//END: fdeventPoll





/*
 * Добавление события для сокета
 */
bool
fdEventSet(fdevent_s * ev, socket_t fd, int events){
	if(fd < 0) RETURN_ERROR(false, "fdEventSet false: FD < 0, fd=[%d], max_fd=[%u]", fd, server_max_fds);
	if(!ev->fds[fd]) RETURN_ERROR(false, "fdEventSet false: FD not exists, fd=[%d], max_fd=[%u]", fd, server_max_fds);
	ev->fds[fd]->events = events;
	int poll_index = ev->fds[fd]->poll_index;

#ifdef USE_EPOLL
	struct epoll_event ep;
	memset(&ep, '\0', sizeof(ep));
	ep.data.fd = fd;
	ep.events = (uint32_t)events;

	if(poll_index != -1){
		//Указанный индекс больше размера массива
		if(poll_index >= server_max_fds) RETURN_ERROR(false, "fdEventSet false: poll_index [%d] > server_max_fds [%u]", poll_index, server_max_fds);
		if(epoll_ctl(ev->epoll_fd, EPOLL_CTL_MOD, fd, &ep) != 0) RETURN_ERROR(false, "epoll_ctl set failed: %s", strerror(errno));
		return true;
	}
	DEBUG_MSG("fdEventSet: (poll_index=%d, fd=%d) <-- add events",poll_index, fd);
	if(epoll_ctl(ev->epoll_fd, EPOLL_CTL_ADD, fd, &ep) != 0) RETURN_ERROR(false, "epoll_ctl add failed: %s", strerror(errno));

	ev->fds[fd]->poll_index = fd;
#endif

#ifdef USE_POLL
	if(poll_index != -1){
		//Указанный индекс больше размера массива
		if(poll_index >= ev->pollfds_count) RETURN_ERROR(false, "fdEventSet false: poll_index [%d] > pollfds_count [%u]", poll_index, ev->pollfds_count);

		if (ev->pollfds[poll_index].fd == fd){
			ev->pollfds[poll_index].events = events;
			return true;
		}
		RETURN_ERROR(false, "fdEventSet false: POLL not exists, fd=[%d], poll_index=[%d]", fd, poll_index);
	}

	ev->pollfds[ev->pollfds_count].fd = fd;
	ev->pollfds[ev->pollfds_count].events = events;
	ev->fds[fd]->poll_index = ev->pollfds_count++;
#endif


	return true;
}//END: fdEventSet



/*
 * "Удаляет" события из сокета
 */
bool
fdEventDelete(fdevent_s * ev, socket_t fd){
	if(fd < 0) RETURN_ERROR(false, "fdEventDelete false: FD < 0, fd=[%d], max_fd=[%u]", fd, server_max_fds);
	if(!ev->fds[fd]) RETURN_ERROR(false, "fdEventDelete false: FD not exists, fd=[%d], max_fd=[%u]", fd, server_max_fds);
	ev->fds[fd]->events = 0;
	int poll_index = ev->fds[fd]->poll_index;
	ev->fds[fd]->poll_index = -1;

#ifdef USE_EPOLL
	//poll_index задан
	if(poll_index != -1){
		if(poll_index >= server_max_fds) RETURN_ERROR(false, "fdEventDelete false: poll_index [%d] > server_max_fds [%u]", poll_index, server_max_fds);
		struct epoll_event ep;
		memset(&ep, '\0', sizeof(ep));
		ep.data.fd = fd;
		if (epoll_ctl(ev->epoll_fd, EPOLL_CTL_DEL, fd, &ep) != 0) RETURN_ERROR(false, "epoll_ctl del failed: %s", strerror(errno));
		return true;
	}//poll_index задан
#endif


#ifdef USE_POLL
	//poll_index задан
	if(poll_index != -1){
		if(!ev->pollfds_count) RETURN_ERROR(false, "fdEventDelete false: pollfds_count is zero");
		if(poll_index >= ev->pollfds_count) RETURN_ERROR(false, "fdEventDelete false: poll_index [%d] > pollfds_count [%u]", poll_index, ev->pollfds_count);

		//Дескрипторы совпадают, все ок
		if(ev->pollfds[poll_index].fd == fd){
			//если удаляемый элемент не последний в pollfds, перемещаем последний элемент на место poll_index
			if(poll_index < ev->pollfds_count-1){
				socket_t tmp_fd	= ev->pollfds[ev->pollfds_count-1].fd;
				int tmp_events	= ev->pollfds[ev->pollfds_count-1].events;
				ev->pollfds[poll_index].fd		= tmp_fd;
				ev->pollfds[poll_index].events	= tmp_events;
				//Не забываем изменить ссылку перемещенного элемента poll_index на новое значение
				if(ev->fds[tmp_fd]!=NULL) ev->fds[tmp_fd]->poll_index = poll_index;
			}
			//Обнуление последнего элемента, уменьшение количества прослушиваемых сокетов на 1
			ev->pollfds[ev->pollfds_count-1].fd = -1;
			ev->pollfds[ev->pollfds_count-1].events = 0;
			ev->pollfds_count--;
			return true;
		}
		RETURN_ERROR(false, "fdEventDelete false: POLL not exists, fd=[%d], poll_index=[%d]", fd, poll_index);
	}//poll_index задан
#endif

	//Если poll_index не задан, это не является ошибкой,
	return true;
	//RETURN_ERROR(true, "fdEventDelete true: poll_index undefined");
}//END: fdEventDelete



/*
 * Возвращает дескриптор сокета по индексу массива pollfds
 */
socket_t
fdEventGetFd(fdevent_s * ev, size_t poll_index){
#ifdef USE_EPOLL
	return poll_index;
	/*
	if(poll_index >= server_max_fds) return -1;
	return ev->pollfds[poll_index].data.fd;
	*/
#endif
#ifdef USE_POLL
	if(!ev->pollfds_count || poll_index >= ev->pollfds_count) return -1;
	return ev->pollfds[poll_index].fd;
#endif
}//END: fdEventGetFd



/*
 * Возвращает следующий индекс массива pollfds, для дескриптора сокета которого есть события или -1, если ничего не найдено
 */
int
fdEventGetNextIndex(fdevent_s * ev, int index){
	size_t i;
	i = (index < 0) ? 0 : index + 1;
#ifdef USE_EPOLL
	return i;
#endif
#ifdef USE_POLL
	for (; i < ev->pollfds_count; i++) {
		if(ev->pollfds[i].revents) return i;
	}
#endif
	return -1;
}//END: fdEventGetNextIndex
