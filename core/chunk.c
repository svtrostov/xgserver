/***********************************************************************
 * XG SERVER
 * core/chunk.c
 * Работа со списком частей контента
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   

#include "core.h"
#include "kv.h"
#include "server.h"


static chunkqueue_s * _chunkqueue_idle_list = NULL;
static chunk_s * _chunk_idle_list = NULL;

//Мьютекс синхронизации в момент обращения к IDLE списку
static pthread_mutex_t chunk_idle_mutex = PTHREAD_MUTEX_INITIALIZER;


#define _toIdle(f_name, d_type, d_list, d_mutex) static void f_name(d_type * item){	\
	if(!item) item = (d_type *)mNewZ(sizeof(d_type));	\
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
_toIdle(_chunkToIdle, chunk_s, _chunk_idle_list, chunk_idle_mutex);

//Получает элемент из IDLE списка или создает новый, если IDLE список пуст
_fromIdle(_chunkFromIdle, chunk_s, _chunk_idle_list, chunk_idle_mutex);



//Добавляет новый/существующмй элемент в IDLE список
_toIdle(_chunkqueueToIdle, chunkqueue_s, _chunkqueue_idle_list, chunk_idle_mutex);

//Получает элемент из IDLE списка или создает новый, если IDLE список пуст
_fromIdle(_chunkqueueFromIdle, chunkqueue_s, _chunkqueue_idle_list, chunk_idle_mutex);



/**
 * Инициализация chunk.c
 */
initialization(chunk_c){
	int i;
	uint32_t chunk_idle_list_size = FD_SETSIZE * 4;
	for(i=0;i<FD_SETSIZE;i++) _chunkqueueToIdle(NULL);
	for(i=0;i<chunk_idle_list_size;i++) _chunkToIdle(NULL);
	DEBUG_MSG("chunk.c initialized.");
}//END: initialization



/***********************************************************************
 * Функции
 **********************************************************************/



/*
 * Создание очереди частей контента
 */
chunkqueue_s *
chunkqueueCreate(void){
	return _chunkqueueFromIdle();
}//END: chunkqueueCreate



/*
 * Удаление очереди
 */
void 
chunkqueueFree(chunkqueue_s * cq){
	if(!cq) return;
	chunk_s * chunk = cq->first;
	chunk_s * current;
	while(chunk){
		switch(chunk->type){

			//Нет контента (не использовать, пропустить)
			case CHUNK_NONE: break;

			//Контент из файла
			case CHUNK_FILE:
				if(chunk->file && chunk->free) requestStaticFileFree(chunk->file);
			break;

			//Контент из буфера buffer_s
			case CHUNK_BUFFER:
				if(chunk->buffer && chunk->free) bufferFree(chunk->buffer);
			break;

			//Контент из структуры типа string_s
			case CHUNK_STRING:
				if(chunk->string && chunk->free) mStringFree(chunk->string);
			break;

			//Контент из области памяти char *
			case CHUNK_HEAP:
				if(chunk->heap && chunk->free) mFree(chunk->heap);
			break;

		}
		current = chunk;
		chunk = chunk->next;
		_chunkToIdle(current);
	}
	if(cq->temp) mFree(cq->temp);
	_chunkqueueToIdle(cq);
}//END: chunkqueueFree



/*
 * Добавляет часть контента в очередь 
 */
chunk_s *
chunkqueueAdd(chunkqueue_s * cq){
	chunk_s * chunk = _chunkFromIdle();
	if(cq->last){
		chunk->prev = cq->last;
		cq->last->next = chunk;
	}
	if(!cq->first){
		cq->first = chunk;
		cq->current.chunk = chunk;
	}
	cq->last = chunk;
	chunk->queue = cq;
	chunk->free = false;
	return chunk;
}//END: chunkqueueAdd




/*
 * Добавляет часть контента в начало очереди
 */
chunk_s *
chunkqueueAddFirst(chunkqueue_s * cq){
	chunk_s * chunk = _chunkFromIdle();
	if(cq->first){
		chunk->next = cq->first;
		cq->first->prev = chunk;
	}
	cq->first = chunk;
	cq->current.chunk = chunk;
	if(!cq->last) cq->last = chunk;
	chunk->queue = cq;
	chunk->free = false;
	return chunk;
}//END: chunkqueueAddFirst




/*
 * Добавляет в очередь файл
 */
chunk_s *
chunkqueueAddFile(chunkqueue_s * cq, static_file_s * sf, uint32_t offset, uint32_t length){
	if(!cq || !sf || offset >= sf->st.st_size || sf->st.st_size <= offset) return NULL;
	if(!length) length = (uint32_t)(sf->st.st_size - offset);
	if(offset + length > sf->st.st_size) return NULL;
	chunk_s * chunk = chunkqueueAdd(cq);
	chunk->type		= CHUNK_FILE;
	chunk->file		= sf;
	chunk->offset	= offset;
	chunk->length	= length;
	chunk->free		= false;
	cq->content_length += length;
	return chunk;
}//END: chunkqueueAddFile



/*
 * Добавляет в очередь буфер
 */
chunk_s *
chunkqueueAddBuffer(chunkqueue_s * cq, buffer_s * buf, uint32_t offset, uint32_t length, bool vfree){
	if(!cq || !buf || buf->count <= offset) return NULL;
	if(!length) length = (uint32_t)(buf->count - offset);
	if(offset + length > buf->count) return NULL;
	chunk_s * chunk = chunkqueueAdd(cq);
	chunk->type		= CHUNK_BUFFER;
	chunk->buffer	= buf;
	chunk->offset	= offset;
	chunk->length	= length;
	chunk->free		= vfree;
	cq->content_length += length;
	return chunk;
}//END: chunkqueueAddBuffer



/*
 * Добавляет в очередь строку
 */
chunk_s *
chunkqueueAddString(chunkqueue_s * cq, string_s * s, uint32_t offset, uint32_t length, bool vfree){
	if(!cq || !s || !s->ptr || !s->len || s->len <= offset) return NULL;
	if(!length) length = (uint32_t)(s->len - offset);
	if(offset + length > s->len) return NULL;
	chunk_s * chunk = chunkqueueAdd(cq);
	chunk->type		= CHUNK_STRING;
	chunk->string	= s;
	chunk->offset	= offset;
	chunk->length	= length;
	chunk->free		= vfree;
	cq->content_length += length;
	return chunk;
}//END: chunkqueueAddString


/*
 * Добавляет в очередь указатель на область памяти
 */
chunk_s *
chunkqueueAddHeap(chunkqueue_s * cq, char * ptr, uint32_t offset, uint32_t length, bool vfree){
	if(!cq || !ptr) return NULL;
	chunk_s * chunk = chunkqueueAdd(cq);
	chunk->type		= CHUNK_HEAP;
	chunk->heap		= ptr;
	chunk->offset	= offset;
	chunk->length	= length;
	chunk->free		= vfree;
	cq->content_length += length;
	return chunk;
}//END: chunkqueueAddHeap



/*
 * Сбросить счетчик прочитанных данных и установить указатель в самое начало
 */
void
chunkqueueReset(chunkqueue_s * cq){
	if(!cq) return;
	cq->current.chunk = cq->first;
	cq->current.written_n = 0;
	cq->temp_size = 0;
	cq->temp_n = 0;
}//END: chunkqueueReset



/*
 * Проверяет, пуста очередь или нет
 */
inline bool
chunkqueueIsEmpty(chunkqueue_s * cq){
	return cq->first ? false : true;
}//END: chunkqueueIsEmpty



/*
 * Читает из очереди очередную порцию контента для отправки клиенту
 */
result_e
chunkqueueRead(chunkqueue_s * cq, const char ** pointer, uint32_t * length){

	if(!cq) goto label_error;
	chunk_s * chunk = cq->current.chunk;
	if(!chunk) goto label_eof;
	int n;

	const char *	send_ptr = NULL;
	uint32_t		send_len = (chunk->length > cq->current.written_n ? chunk->length - cq->current.written_n : 0);

	label_chunk:

	//Больше нет данных для отправки в этой части контента
	if(!send_len){
		cq->current.chunk = chunk = chunk->next;
		//EOF
		if(!chunk) goto label_eof;
		cq->current.written_n = 0;
		cq->temp_size = 0;
		cq->temp_n = 0;
	}

	switch(chunk->type){

		//Нет контента (не использовать, пропустить) - 
		case CHUNK_NONE:
			send_ptr = NULL;
			send_len = 0;
			goto label_chunk;
		break;

		//Контент из файла
		case CHUNK_FILE:
			if(!cq->temp){
				cq->temp = mNew(chunkqueue_internal_buffer_size + 1);
				cq->temp[chunkqueue_internal_buffer_size] = '\0';
				cq->temp_size = 0;
				cq->temp_n = 0;
			}
			//Если внутренний буфер передан не полностью - отправка оставшихся данных
			if(cq->temp_n < cq->temp_size){
				send_ptr = (const char *)&cq->temp[cq->temp_n];
				send_len = cq->temp_size - cq->temp_n;
			}
			//Если во внутреннем буфере нет данных или все отправлены - чтение новой порции из файла
			else{
				//Это первое чтение данных из текущего куска 
				if(cq->temp_size == 0){
					if(lseek(chunk->file->fd, chunk->offset, SEEK_SET) == -1){
						goto label_error;
					}
				}
				n = read(chunk->file->fd, cq->temp, min(chunkqueue_internal_buffer_size, chunk->length - cq->current.written_n));
				if(n == 0){
					send_len = 0;
					goto label_chunk;
				}
				if(n <  0){
					goto label_error;
				}
				send_len = n;
				send_ptr = cq->temp;
				cq->temp_size = n;
				cq->temp_n = 0;
			}
		break;

		//Контент из буфера buffer_s
		case CHUNK_BUFFER:
			if(cq->current.written_n < chunk->length){
				send_ptr = (const char *)&chunk->buffer->buffer[chunk->offset + cq->current.written_n];
				send_len = chunk->length - cq->current.written_n;
			}else{
				send_len = 0;
				goto label_chunk;
			}
		break;

		//Контент из структуры типа string_s
		case CHUNK_STRING:
			if(cq->current.written_n < chunk->length){
				send_ptr = (const char *)&chunk->string->ptr[chunk->offset + cq->current.written_n];
				send_len = chunk->length - cq->current.written_n;
			}else{
				send_len = 0;
				goto label_chunk;
			}
		break;

		//Контент из области памяти char *
		case CHUNK_HEAP:
			if(cq->current.written_n < chunk->length){
				send_ptr = (const char *)&chunk->heap[chunk->offset + cq->current.written_n];
				send_len = chunk->length - cq->current.written_n;
			}else{
				send_len = 0;
				goto label_chunk;
			}
		break;

	}

	if(length) *length = send_len;
	if(pointer)*pointer = send_ptr;
	return RESULT_OK;


	label_error:
		if(length) *length = 0;
		if(pointer)*pointer = NULL;
		return RESULT_ERROR;

	label_eof:
		if(pointer)*pointer = NULL;
		if(length) *length = 0;
		return RESULT_EOF;
}//END: chunkqueueRead



/*
 * Вызов функции "говорит" очереди о том, что было успешно отправлено length байт данных,
 * полученных из chunkqueueRead и курсор отправки можно перемещать вперед на length байт
 */
void
chunkqueueCommit(chunkqueue_s * cq, uint32_t length){
	if(!cq) return;
	cq->current.written_n += length;
	cq->temp_n += length;
}//END: chunkqueueCommit





/*
 * Устанавливает буфер с заголовками в начале очереди
 */
chunk_s *
chunkqueueSetHeaderBuffer(chunkqueue_s * cq, buffer_s * buf, bool vfree){
	if(!cq || !buf) return NULL;
	chunk_s * chunk = chunkqueueAddFirst(cq);
	chunk->type		= CHUNK_BUFFER;
	chunk->buffer	= buf;
	chunk->offset	= 0;
	chunk->length	= buf->count;
	chunk->free		= vfree;
	return chunk;
}//END: chunkqueueSetHeaderBuffer







