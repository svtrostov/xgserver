/***********************************************************************
 * XG SERVER
 * core/socket.c
 * Функции работы с сокетом
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include "server.h"

/***********************************************************************
 * Функции
 **********************************************************************/


/*
 * Функция закрывает ранее открытый сокет
 */
void
socketClose(socket_t sock_fd){
	if(sock_fd == -1) return;
	uint32_t n = 10;
	shutdown(sock_fd, SHUT_RDWR);
	while(close(sock_fd)==-1 && errno == EINTR && n > 0) n--;
	return;
}


/*
 * Установка сокета в блокируемое (не блокируемое) состояние
 * возвращает 0 в случае успеха либо -1 в случае ошибки
 */
int
socketSetNonblockState(socket_t sock_fd, bool as_nonblock){
	int flags;
	if(sock_fd == -1) return -1;
	if((flags = fcntl(sock_fd, F_GETFL, 0)) == -1) return -1;
	if(as_nonblock)
		return (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) == -1 ? -1 : 0);
	else
		return (fcntl(sock_fd, F_SETFL, flags & (~O_NONBLOCK)) == -1 ? -1 : 0);
}//END: socketSetNonblockState



/*
 * Чтение N байт из сокета с сохранением содержимого
 */
result_e
socketReadPeek(socket_t sock_fd, char * buf, size_t ilen, uint32_t *olen){
	int n = recv(sock_fd, buf, ilen, MSG_PEEK);
	if(n > 0){
		if(olen) *olen = n;
		return RESULT_OK;
	}
	if(olen) *olen = 0;
	if(n == 0) return RESULT_EOF;

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

}//END: socketReadPeek




/*
 * Чтение N байт из сокета
 */
result_e
socketRead(socket_t sock_fd, char * buf, size_t ilen, uint32_t *olen){
	int n = read(sock_fd, buf, ilen);
	if(n > 0){
		if(olen) *olen = n;
		return RESULT_OK;
	}
	if(olen) *olen = 0;
	if(n == 0) return RESULT_EOF;

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

}//END: socketRead



/*
 * Запись N байт в сокет
 */
result_e
socketWrite(socket_t sock_fd, const char * buf, size_t ilen, uint32_t *olen){
	int n = write(sock_fd, buf, ilen);
	if(n > 0){
		if(olen) *olen = n;
		return RESULT_OK;
	}
	if(olen) *olen = 0;
	if(n == 0) return RESULT_EOF;

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

}//END: socketWrite

