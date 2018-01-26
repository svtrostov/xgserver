/***********************************************************************
 * XG SERVER
 * core/buffer.c
 * Работа с буфером данных
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/  


#include <time.h>
#include <sys/types.h> 
#include <unistd.h>
#include <fcntl.h>
#include "core.h"
#include "server.h"


static buffer_s * _buffer_idle_list = NULL;

//Мьютекс синхронизации в момент обращения к IDLE списку
static pthread_mutex_t buffer_idle_mutex = PTHREAD_MUTEX_INITIALIZER;


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
_toIdle(_bufferToIdle, buffer_s, _buffer_idle_list, buffer_idle_mutex);

//Получает элемент из IDLE списка или создает новый, если IDLE список пуст
_fromIdle(_bufferFromIdle, buffer_s, _buffer_idle_list, buffer_idle_mutex);



/**
 * Инициализация buffer.c
 */
initialization(buffer_c){
	int i;
	uint32_t buffer_idle_list_size = FD_SETSIZE * 8;
	for(i=0;i<buffer_idle_list_size;i++) _bufferToIdle(NULL);
	DEBUG_MSG("buffer.c initialized.");
}//END: initialization




/***********************************************************************
 * Функции - 
 **********************************************************************/

/*
 * Вывод буфера на экран (для отладки)
 */
void
bufferPrint(buffer_s * buf){
	if(!buf){
		printf("\nbuffer is NULL\n");
		return;
	}
	printf("\nbuffer->increment = [%u]\n", buf->increment);
	printf("buffer->allocated = [%u]\n", buf->allocated);
	printf("buffer->count = [%u]\n", buf->count);
	printf("buffer->index = [%u]\n", buf->index);
	printf("buffer->buffer = \n[BEGIN:[%s]:END]\n\n", buf->buffer);
	return;
}//END: bufferPrint



/*
 * Создание буфера
 */
buffer_s *
bufferCreate(uint32_t increment){
	buffer_s * buf	= _bufferFromIdle();
	buf->increment	= (!increment ? buffer_s_default_increment : increment);
	return bufferClear(buf);
}//END: bufferCreate



/*
 * Очистка буфера и приведение его в исходное состояние
 */
buffer_s *
bufferClear(buffer_s * buf){
	buf->allocated	= buf->increment;
	buf->buffer		= mResize(buf->buffer, buf->increment);
	buf->count		= 0;
	buf->index		= 0;
	buf->buffer[0]	= '\0';
	return buf;
}//END: bufferClear



/*
 * Освобождение памяти, занятой под буфера
 */
void
bufferFree(buffer_s * buf){
	if(!buf) return;
	mFree(buf->buffer);
	_bufferToIdle(buf);
}//END: bufferFree



/*
 * Изменение значения инкремента
 */
buffer_s *
bufferSetIncrement(buffer_s * buf, uint32_t increment){
	buf->increment = (increment > 0 ? increment : buffer_s_default_increment);
	return buf;
}//END: bufferSetIncrement



/*
 * Устанавливает курсор в буфере на определенную позицию
 */
void
bufferSeekSet(buffer_s * buf, int32_t offset, seek_position_e origin){
	switch(origin){
		case R_SEEK_START:
			buf->index = max(0, min(offset, buf->count));
		break;
		case R_SEEK_CURRENT:
			buf->index = max(0, min(buf->index + offset, buf->count));
		break;
		case R_SEEK_END:
			buf->index = max(0, min(buf->count - offset, buf->count));
		break;
		default: break;
	}
	return; 
}//END: bufferSetIndex



/*
 * Устанавливает курсор на последнюю позицию \0 для продолжения буфера
 */
inline void
bufferSeekEnd(buffer_s * buf){
	buf->index = buf->count;
}//END: bufferSeekEnd



/*
 * Устанавливает курсор на первую позицию
 */
inline void
bufferSeekBegin(buffer_s * buf){
	buf->index = 0;
}//END: bufferSeekEnd



/*
 * Возвращает текущюю позицию курсора
 */
inline uint32_t
bufferGetPos(buffer_s * buf){
	return buf->index;
}//END: bufferGetPos



/*
 * Возвращает указатель на текущюю позицию в буфере
 */
inline char *
bufferGetPtr(buffer_s * buf){
	return &(buf->buffer[buf->index]);
}//END: bufferGetPtr



/*
 * Возвращает доступное количество байт в буфере считая с текущей позиции
 */
inline uint32_t
bufferGetAllowedSize(buffer_s * buf){
	return (buf->allocated - buf->index);
}//END: bufferGetAllowedSize



/*
 * Увеличивает размер буфера, если выделенный размер буфера недостаточен
 */
void
bufferIncrease(buffer_s * buf, uint32_t need){
	need += buf->index + 1;
	if(buf->allocated < need){
		while(buf->allocated < need) buf->allocated += buf->increment;
		buf->buffer = mResize(buf->buffer, buf->allocated);
	}
	return;
}//END: bufferCheckSize



/*
 * Добавление строки str в буфер
 */
void
bufferAddString(buffer_s * buf, const char * str){
	if(!str) return;
	bufferSetString(buf, str);
	buf->buffer[buf->index] = '\0';
	buf->count = buf->index;
}//END: bufferAddString



/*
 * Заменяет строкой str часть буфера
 */
void
bufferSetString(buffer_s * buf, const char * str){
	if(!str) return;
	char * ptr = &buf->buffer[buf->index];
	while(*str){
		*ptr++ = *str++;
		buf->index++;
		if(buf->index >= buf->allocated){
			bufferIncrease(buf, 1);
			ptr = &buf->buffer[buf->index];
		}
	}
	if(buf->index > buf->count){
		buf->buffer[buf->index] = '\0';
		buf->count = buf->index;
	}
	return;
}//END: bufferSetString




/*
 * Добавление n символов из строки str в буфер
 */
void
bufferAddStringN(buffer_s * buf, const char * str, uint32_t len){
	if(!str) return;
	if(!len) bufferSetString(buf, str);
	else bufferSetStringN(buf, str, len);
	buf->buffer[buf->index] = '\0';
	buf->count = buf->index;
}//END: bufferAddStringN



/*
 * Заменяет n символов из строки str в буфере
 */
void
bufferSetStringN(buffer_s * buf, const char * str, uint32_t len){
	if(!str) return;
	uint32_t n = 0;
	bufferIncrease(buf, len);
	char * ptr = &buf->buffer[buf->index];
	while(*str && n<len){
		*ptr++ = *str++;
		n++;
	}
	buf->index += n;
	if(buf->index > buf->count){
		buf->buffer[buf->index] = '\0';
		buf->count = buf->index;
	}
	return;
}//END: bufferSetStringN



/*
 * Добавление size байт из области памяти ptr в буфер
 */
void
bufferAddHeap(buffer_s * buf, const char * ptr, uint32_t size){
	bufferSetHeap(buf, ptr, size);
	buf->buffer[buf->index] = '\0';
	buf->count = buf->index;
}//END: bufferAddHeap



/*
 * Установка size байт из области памяти ptr в буфер
 */
void
bufferSetHeap(buffer_s * buf, const char * ptr, uint32_t size){
	if(!ptr) return;
	bufferIncrease(buf, size);
	memcpy(&(buf->buffer[buf->index]), ptr, size);
	buf->index += size;
	if(buf->index > buf->count){
		buf->buffer[buf->index] = '\0';
		buf->count = buf->index;
	}
	return;
}//END: bufferSetHeap



/*
 * Добавление символа в буфер
 */
void
bufferAddChar(buffer_s * buf, u_char ch){
	bufferSetChar(buf, ch);
	buf->buffer[buf->index] = '\0';
	buf->count = buf->index;
}//END: bufferAddChar



/*
 * Установка символа в буфер
 */
void
bufferSetChar(buffer_s * buf, u_char ch){
	bufferIncrease(buf, 1);
	buf->buffer[buf->index] = ch;
	buf->index++;
	if(buf->index > buf->count){
		buf->buffer[buf->index] = '\0';
		buf->count = buf->index;
	}
	return;
}//END: bufferSetChar



/*
 * Добавление HEX значения символа в буфер
 */
void
bufferAddHex(buffer_s * buf, u_char ch){
	bufferSetHex(buf, ch);
	buf->buffer[buf->index] = '\0';
	buf->count = buf->index;
}//END: bufferAddHex



/*
 * Установка HEX значения символа в буфер
 */
void
bufferSetHex(buffer_s * buf, u_char ch){
	bufferIncrease(buf, 2);
	buf->buffer[buf->index++] = hexTable[ch >> 4];
	buf->buffer[buf->index++] = hexTable[ch & 0xf];
	if(buf->index > buf->count){
		buf->buffer[buf->index] = '\0';
		buf->count = buf->index;
	}
	return;
}//END: bufferSetHex



/*
 * Добавление числа в буфер
 */
void
bufferAddInt(buffer_s * buf, int64_t v){
	bufferIncrease(buf, NDIG);
	buf->index += intToStringPtr(v, &buf->buffer[buf->index]);
	buf->count = buf->index;
}//END: bufferAddInt



/*
 * Установка числа в буфер
 */
void
bufferSetInt(buffer_s * buf, int64_t v){
	bufferIncrease(buf, NDIG);
	buf->index += intToStringPtr(v, &buf->buffer[buf->index]);
	if(buf->index > buf->count) buf->count = buf->index;
}//END: bufferSetInt



/*
 * Добавление числа в буфер
 */
void
bufferAddDouble(buffer_s * buf, double v){
	bufferIncrease(buf, NDIG);
	buf->index += doubleToStringPtr(v, 6, &buf->buffer[buf->index]);
	buf->count = buf->index;
}//END: bufferAddDouble



/*
 * Установка числа в буфер
 */
void
bufferSetDouble(buffer_s * buf, double v){
	bufferIncrease(buf, NDIG);
	buf->index += doubleToStringPtr(v, 6, &buf->buffer[buf->index]);
	if(buf->index > buf->count) buf->count = buf->index;
}//END: bufferSetDouble



/*
 * Функция добавляет в буфер текстовую строку заданного формата
 * Поддерживаемые форматы строки:
 * %s - строка
 * %d - целое число
 * %f - double число
 * %% - символ %
 * %t - timestamp в секундах
 * %g - timestamp в формате GMT
 */
void
bufferAddStringFormat(buffer_s * buf, const char * fmt, ...){

	va_list ap;

	register char * ptr;
	register const char * e = fmt;
	char tmp[64];
	time_t ts;
	struct tm tm;


	va_start(ap, fmt);
	bufferIncrease(buf, 1); //увеличение размера буфера

	while(1){

		ptr = &buf->buffer[buf->index];

		//Ищем первое вхождение символа % или конец строки
		//А также заодно копируем нефоратируемые символы из fmt в буфер
		while(*e!='%' && *e!='\0'){
			*ptr++ = *e++;
			if(++buf->index >= buf->allocated){
				bufferIncrease(buf, 1);
				ptr = &buf->buffer[buf->index];
			}
		}
		*ptr = '\0';
		buf->count = buf->index;

		if(*e=='%'){
			e++;
			switch(*e){
				//Вставка строки
				case 's':
					bufferAddString(buf, va_arg(ap, char *));
					e++;
				break;
				//Вставка целого числа
				case 'd':
					bufferAddInt(buf, va_arg(ap, int64_t));
					e++;
				break;
				//Вставка дробного числа
				case 'f':
					bufferAddDouble(buf, va_arg(ap, double));
					e++;
				break;
				//Символ %
				case '%':
					bufferAddChar(buf, '%');
					e++;
				break;
				//timestamp секунды
				case 't':
					ts = va_arg(ap, time_t);
					bufferAddInt(buf, (int64_t)ts);
					e++;
				break;
				//GMT timestamp
				case 'g':
					ts = va_arg(ap, time_t);
					localtime_r(&ts, &tm);
					strftime(tmp, sizeof(tmp), "%a, %d %b %Y %H:%M:%S GMT", &tm);
					bufferAddString(buf, tmp);
					e++;
				break;
				//Другое не обрабатываем
				default: break;
			}
		}//*e == '%'

		if(*e == '\0') break;
	}//while

	va_end(ap);

}//END: bufferAddStringFormat




/*
 * Добавление даты и времени в буфер
 */
void
bufferAddDatetime(buffer_s * buf, time_t ts, const char * format){
	char tmp[128];
	struct tm tm;
	localtime_r(&ts, &tm);
	size_t n = strftime(tmp, sizeof(tmp)-1, format, &tm);
	bufferAddStringN(buf, tmp, n);
}//END: bufferAddDatetime




/*
 * Установка даты и времени в буфер
 */
void
bufferSetDatetime(buffer_s * buf, time_t ts, const char * format){
	char tmp[128];
	struct tm tm;
	localtime_r(&ts, &tm);
	size_t n = strftime(tmp, sizeof(tmp)-1, format, &tm);
	bufferSetStringN(buf, tmp, n);
}//END: bufferSetDatetime



/*
 * Сохранение буфера в файл
 */
bool
bufferSaveToFile(buffer_s * buf, const char * filename, const char * path){
	if(!filename) return false;
	char * tmp = NULL;
	const char * localfile = NULL;
	if(path){
		tmp = pathConcatS(path, filename, NULL);
		localfile = tmp;
	}else{
		localfile = filename;
	}

	//Открытие файла на запись
	int fd = open(localfile, O_CREAT | O_WRONLY, 0644);
	if(fd < 0){
		if(tmp) mFree(tmp);
		return false;
	}

	if(tmp) mFree(tmp);

	//Запись файла
	int64_t n = write(fd, buf->buffer, buf->count);
	close(fd);

	if(n != buf->count) return false;
	return true;
}//END: bufferSaveToFile




/*
 * Загрузка буфера из файла
 */
buffer_s *
bufferLoadFromFile(const char * filename, const char * path){

	if(!filename) return NULL;
	char * tmp = NULL;
	const char * localfile = NULL;
	if(path){
		tmp = pathConcatS(path, filename, NULL);
		localfile = tmp;
	}else{
		localfile = filename;
	}

	struct stat st;
	if(stat(localfile, &st)!=0){
		if(tmp) mFree(tmp);
		return NULL;
	}
	int64_t len = (int64_t)st.st_size;
	if(len > buffer_s_max_load_size || BIT_ISUNSET(st.st_mode, S_IFREG)){
		if(tmp) mFree(tmp);
		return NULL;
	}

	//Открытие файла на запись
	int fd = open(localfile, O_RDONLY);
	if(fd < 0){
		if(tmp) mFree(tmp);
		return false;
	}

	if(tmp) mFree(tmp);

	buffer_s * buf = bufferCreate(0);
	buf->buffer[0] = '\0';
	if(len == 0) return buf;
	bufferIncrease(buf, len);

	//Чтение из файла
	int64_t n = read(fd, buf->buffer, (size_t)len);
	close(fd);

	if(n != len){
		bufferFree(buf);
		return NULL;
	}
	buf->index = (uint32_t)n;
	buf->buffer[buf->index] = '\0';
	buf->count = buf->index;

	return buf;
}//END: bufferLoadFromFile




