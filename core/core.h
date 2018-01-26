/***********************************************************************
 * XGSERVER
 * core.h
 * Заголовки ядра
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/


#ifndef _XGCORE_H
#define _XGCORE_H

#ifdef __cplusplus
extern "C" {
#endif


/***********************************************************************
 * Подключаемые заголовки
 **********************************************************************/
#include <locale.h>
#include <stdbool.h>	//true false
#include <stdint.h>		//uint32_t
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>		//struct timeval, struct timespec, clock_gettime, nanosleep
#include <sys/time.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <sys/types.h>	//u_char
#include <ctype.h>		//tolower, toupper
#include <string.h>		//strlen, strcpy, memcpy, memmove, strcat, strchr и т.д.
#include <strings.h>	//bzero, bcopy, strcasecmp, strncasecmp
#include <stdio.h>		//printf, sprintf
#include <inttypes.h>	//PRIu64
#include <dirent.h>		//readdir
#include <limits.h>		//PATH_MAX
#include <stdarg.h>		//va_start, va_arg
#include <unistd.h>		//getcwd
#include <pthread.h>	//threads
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <assert.h>
#include <malloc.h>
#include "defines.h"
#include "event.h"
#include <openssl/sha.h>

#ifndef NDIG
#define NDIG 64
#endif

# ifndef __u_char_defined
typedef unsigned char u_char;
#endif

#ifndef XG_SERVER_VERSION
#define XG_SERVER_VERSION "XGServer/20141203 Alpha"
#endif


#define XG_DATETIME_GMT_FORMAT "%a, %d %b %Y %H:%M:%S GMT"

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#define BIT(x) (1 << x)
#define BIT_ISSET(variable, flag)((variable & flag) == flag)
#define BIT_ISUNSET(variable, flag)((variable & flag) != flag)
#define BIT_SET(variable, n) do{variable |= n;}while(0)
#define BIT_UNSET(variable, n) do{variable &= ~n;}while(0)
#define BIT_TOGGLE(variable, n) do{variable ^= n;}while(0)

#define CONST_STR_COMMA_LEN(s) s, (s ? (uint32_t)(sizeof(s) - 1) : (uint32_t)0)

#define XG_ASSERT(expr) do{ \
	if(!(expr)){ \
		printf("ASSERT FAIL ON "__FILE__" LINE %d\n", __LINE__);	\
		char * ___sigseg_ptr = NULL; \
		*___sigseg_ptr = 0; \
	} \
}while(0)


#ifdef XG_DEBUG
pthread_mutex_t	XG_DEBUG_MUTEX;
#define DEBUG_MSG(format, ...)do{	\
	printf(""__FILE__" |%d| "format"\n", __LINE__, ##__VA_ARGS__);	\
}while(0)
#else
#define DEBUG_MSG(format, ...)do{}while(0) 
#endif

#ifdef XG_ERROR
#define ERROR_MSG(format, ...)do{	\
	printf(""__FILE__" |%d| "format"\n", __LINE__, ##__VA_ARGS__);	\
}while(0)
#else
#define ERROR_MSG(format, ...)do{}while(0) 
#endif

#define RETURN_ERROR(ret, format, ...) do{ERROR_MSG(format, ##__VA_ARGS__); return ret;}while(0)

#define FATAL_ERROR(format, ...)											\
do{																			\
	fprintf(stderr, ""__FILE__" |%d| "format"\n", __LINE__, ##__VA_ARGS__);	\
	exit(-1);																\
}while(0)



//Размер IPv4 адреса
#define IPV4_SIZE sizeof(struct in_addr)

//Длинна строки для IPv4
#define IPV4_LEN 16

//Размер IPv6 адреса
#define IPV6_SIZE sizeof(struct in6_addr)

//Длинна строки для IPv6
#define IPV6_LEN 46

//Максимальный размер IP адреса
#define IP_SIZE	IPV6_SIZE
#define IP_LEN	IPV6_LEN



/*
 * Атрибут constructor заставляет функцию вызываться автоматически перед выполнением main (). 
 * Аналогично, атрибут destructor заставляет функцию вызываться автоматически после того, 
 * как main () завершилась или вызвана exit (). 
 * Функции с этими атрибутами полезны для инициализации данных.
 */
#define initialization(fn) __attribute__ ((constructor)) static void _init_fn()
#define finalization(fn) __attribute__ ((destructor)) static void _destroy_fn()


/*
Прототип пользовательской функции, отвечающий
за уничтожение данных
void * - указатель на данные
*/
typedef void (*free_cb)(void *);


/***********************************************************************
 * Константы
 **********************************************************************/

//Размер инкремента для увеличения размера буфера в структуре buffer_s
static const uint32_t buffer_s_default_increment = 1024;

//Максимальный размер загружаемого в буфер файла (для bufferLoadFromFile())
static const uint32_t buffer_s_max_load_size = 1024 * 1024;


static const char digits[] = "0123456789abcdef";
static const char hexTable[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};


static const char  _us_chars[62] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";	//Символьный массив

static const u_char unreserved_chars[256] = {
	/*
	0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	*/
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /*  00 -  0F control chars */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /*  10 -  1F */
	1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1,  /*  20 -  2F space " # $ % & ' + , / */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,  /*  30 -  3F : ; < = > ? */
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /*  40 -  4F @ */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0,  /*  50 -  5F [ \ ] ^ */
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /*  60 -  6F ` */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,  /*  70 -  7F { | } ~ DEL */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /*  80 -  8F */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /*  90 -  9F */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /*  A0 -  AF */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /*  B0 -  BF */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /*  C0 -  CF */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /*  D0 -  DF */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /*  E0 -  EF */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1   /*  F0 -  FF */
};

#define CHAR_INDEX_MAX 61
#define CHAR_INDEX_COUNT CHAR_INDEX_MAX + 1
//Массив получения индекса из символа - цифры и текст
static const int _char_index[256] = {
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	0,	1,	2,	3,	4,	5,	6,	7,	8,	9,	-1,	-1,	-1,	-1,	-1,	-1,	/* 0123456789...... */
	-1,	10,	11,	12,	13,	14,	15,	16,	17,	18,	19,	20,	21,	22,	23,	24,	/* .ABCDEFGHIJKLMNO */
	25,	26,	27,	28,	29,	30,	31,	32,	33,	34,	35,	-1,	-1,	-1,	-1,	-1,	/* PQRSTUVWXYZ..... */
	-1,	36,	37,	38,	39,	40,	41,	42,	43,	44,	45,	46,	47,	48,	49,	50,	/* .abcdefghijklmno */
	51,	52,	53,	54,	55,	56,	57,	58,	59,	60,	61,	-1,	-1,	-1,	-1,	-1,	/* pqrstuvwxyz..... */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1	/* ................ */
};

#define PRINT_INDEX_MAX 94
#define PRINT_INDEX_COUNT PRINT_INDEX_MAX + 1
//Массив получения индекса из символа - печатные символы
static const int _print_index[256] = {
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	0,	1,	2,	3,	4,	5,	6,	7,	8,	9,	10,	11,	12,	13,	14,	15,	/*  !"#$%&'()*+,-./ */
	16,	17,	18,	19,	20,	21,	22,	23,	24,	25,	26,	27,	28,	29,	30,	31,	/* 0123456789:;<=>? */
	32,	33,	34,	35,	36,	37,	38,	39,	40,	41,	42,	43,	44,	45,	46,	47,	/* @ABCDEFGHIJKLMNO */
	48,	49,	50,	51,	52,	53,	54,	55,	56,	57,	58,	59,	60,	61,	62,	63,	/* PQRSTUVWXYZ[\]^_ */
	64,	65,	66,	67,	68,	69,	70,	71,	72,	73,	74,	75,	76,	77,	78,	79,	/* `abcdefghijklmno */
	80,	81,	82,	83,	84,	85,	86,	87,	88,	89,	90,	91,	92,	93,	94,	-1,	/* pqrstuvwxyz{|}~. */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	/* ................ */
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1	/* ................ */
};


/***********************************************************************
 * Объявления и декларации
 **********************************************************************/

//Тип увеличения значения
typedef enum{
	SIZE_SET	= 0,	//Установить значение как указано
	SIZE_INC	= 1,	//Увеличить текущее значение
	SIZE_DEC	= -1	//Уменьшить текущее значение
} size_e;



//Тип позиции относительно контента
typedef enum{
	R_SEEK_START,		//От начала (файла, строки)
	R_SEEK_CURRENT,		//От текущей позиции
	R_SEEK_END			//От конца (файла, строки)
} seek_position_e;



/***********************************************************************
 * Структуры
 **********************************************************************/

typedef struct		type_buffer_s			buffer_s;		//Структура буфера данных
typedef struct		type_extension_s		extension_s;	//Структура расширения .so
typedef struct		type_extensions_s		extensions_s;	//Структура расширения .so


//Структура адреса для семейства протоколовTCP/IP
typedef union{
	struct sockaddr_in ipv4;	//IPv4
	struct sockaddr_in6 ipv6;	//IPv6
	struct sockaddr plain;		//Общий шаблон адреса
} socket_addr_s;


//Стркутура хранения текстовой строки
typedef struct{
	char *		ptr;	//Указатель на начало строки
	uint32_t	len;	//Длинна строки
} string_s;


//Стркутура хранения текстовой строки
typedef struct{
	const char *	ptr;	//Указатель на начало строки
	uint32_t		len;	//Длинна строки
} const_string_s;



//Структура буфера данных
typedef struct type_buffer_s{
	char * buffer;			//Буфер данных
	uint32_t count;			//Длинна доступных данных
	uint32_t index;			//Текущая позиция курсора в буфере
	uint32_t allocated;		//Выделенный размер блока памяти
	uint32_t increment;		//Размер увеличения блока памяти
	buffer_s * next;		//для IDLE списка
} buffer_s;


//Структура расширения .so
typedef struct type_extension_s{
	void			* handle;		//Указатель полученный от dlopen()
	char			* filename;		//Имя файла расширения
	extension_s		* next;			//Указатель на следующее расширение
} extension_s;


//Структура расширения .so
typedef struct type_extensions_s{
	extension_s		* first;	//Указатель на первое расширение
	extension_s		* last;		//Указатель на последнее расширение
	uint32_t		count;		//Количество расширений
} extensions_s;



/***********************************************************************
 * Глобальные переменные
 **********************************************************************/






/***********************************************************************
 * Функции: core/application.c - Работа с приложением
 **********************************************************************/






/***********************************************************************
 * Функции: core/memory.c - Работа с памятью
 **********************************************************************/

#ifdef XG_MEMSTAT
void		mStatStart(void);	//
void		mStatStop(void);	//
void		mStatPrint(void);	//
#endif

#ifdef XG_MEM_USE_CACHE
void		mCacheOn(void);		//
void		mCacheOff(void);	//
#endif

inline void *	mZero(void * ptr, size_t size);	//Заполнение size байт блока памяти нулями, начиная с ptr 
inline void *	mNew(size_t size);	//Выделение блока памяти malloc
inline void *	mNewZ(size_t size);	//Выделение блока памяти malloc c обнулением выделенного диапазона
inline void *	mCalloc(size_t count, size_t size);	//Выделение блока памяти calloc
inline void *	mRealloc(void * ptr, size_t size);	//Изменение размера блока памяти realloc
inline void *	mResize(void * ptr, size_t size);	//Изменение размера блока памяти realloc
inline void		mFree(void * ptr);	//Освобождение блока памяти free
inline void		mFreeAndNull(void ** ptr);	//Освобождение блока памяти free, установка ptr в NULL и возврат ptr

inline string_s *	mStringNew(void);	//Создание новой структуры string_s
inline string_s * 	mStringClear(string_s * str);	//Очистка структуры string_s
inline void			mStringFree(string_s * str);	//Освобождение памяти free для структуры string_s
inline void			mStringFreeAndNull(string_s ** str);	//Освобождение памяти free для структуры string_s, установка str в NULL




/***********************************************************************
 * Функции: core/buffer.c - Работа с буфером данных
 **********************************************************************/

void		bufferPrint(buffer_s * buf);	//Вывод буфера на экран (для отладки)
buffer_s *	bufferCreate(uint32_t increment);	//Создание буфера
buffer_s *	bufferClear(buffer_s * buf);	//Очистка буфера и приведение его в исходное состояние
void		bufferFree(buffer_s * buf);	//Освобождение памяти, занятой под буфера
buffer_s *	bufferSetIncrement(buffer_s * buf, uint32_t increment);	//Изменение значения инкремента
void		bufferIncrease(buffer_s * buf, uint32_t need);	//Увеличивает размер буфера, если выделенный размер буфера недостаточен
void		bufferSetString(buffer_s * buf, const char * str);	//Заменяет строкой str часть буфера
void		bufferAddString(buffer_s * buf, const char * str);	//Добавление строки str в буфер
void		bufferSetStringN(buffer_s * buf, const char * str, uint32_t len);	//Заменяет n символов из строки str в буфере
void		bufferAddStringN(buffer_s * buf, const char * str, uint32_t len);	//Добавление строки str в буфер
void		bufferAddHeap(buffer_s * buf, const char * ptr, uint32_t size);	//Добавление size байт из области памяти ptr в буфер
void		bufferSetHeap(buffer_s * buf, const char * ptr, uint32_t size);	//Установка size байт из области памяти ptr в буфер
void		bufferAddChar(buffer_s * buf, u_char ch);	//Добавление символа в буфер
void		bufferSetChar(buffer_s * buf, u_char ch);	//Установка символа в буфер
void		bufferAddHex(buffer_s * buf, u_char ch);	//Добавление HEX значения символа в буфер
void		bufferSetHex(buffer_s * buf, u_char ch);	//Установка HEX значения символа в буфер
void		bufferAddInt(buffer_s * buf, int64_t v);	//Добавление числа int64 в буфер
void		bufferSetInt(buffer_s * buf, int64_t v);	//Установка числа int64 в буфер
void		bufferAddDouble(buffer_s * buf, double v);	//Добавление числа double в буфер
void		bufferSetDouble(buffer_s * buf, double v);	//Установка числа double в буфер
inline void		bufferSeekSet(buffer_s * buf, int32_t offset, seek_position_e origin);	//Устанавливает курсор в буфере на определенную позицию
inline void		bufferSeekEnd(buffer_s * buf);	//Устанавливает курсор на последнюю позицию \0 для продолжения буфера
inline void		bufferSeekBegin(buffer_s * buf);	//Устанавливает курсор на первую позицию
inline uint32_t	bufferGetPos(buffer_s * buf);	//Возвращает текущюю позицию курсора
inline char *	bufferGetPtr(buffer_s * buf);	//Возвращает указатель на текущюю позицию в буфере
inline uint32_t	bufferGetAllowedSize(buffer_s * buf);	//Возвращает доступное количество байт в буфере считая с текущей позиции
void		bufferAddStringFormat(buffer_s * buf, const char * fmt, ...);	//Функция добавляет в буфер текстовую строку заданного формата
void		bufferAddDatetime(buffer_s * buf, time_t ts, const char * format);	//Добавление даты и времени в буфер
void		bufferSetDatetime(buffer_s * buf, time_t ts, const char * format);	//Установка даты и времени в буфер
bool		bufferSaveToFile(buffer_s * buf, const char * filename, const char * path);	//Сохранение буфера в файл
buffer_s *	bufferLoadFromFile(const char * filename, const char * path);	//Загрузка буфера из файла


/***********************************************************************
 * Функции: core/utils.c - Прикладные функции
 **********************************************************************/


/*Хэш функции*/
void 		hashSHA256(char * output, const char * input, uint32_t ilen);	//Вычисляет хеш строки по алгоритму SHA256
uint32_t	hashString(const char * str, uint32_t * olen);	//Вычисляет хэш строки
uint32_t	hashStringN(const char * str, uint32_t ilen, uint32_t * olen);	//Вычисляет хэш n символов строки
uint32_t	hashStringCase(const char * str, uint32_t * olen);	//Вычисляет хэш строки без учета регистра
uint32_t	hashStringCaseN(const char * str, uint32_t ilen, uint32_t * olen);	//Вычисляет хэш n символов строки без учета регистра
char *		hashStringCloneCaseN(const char * str, uint32_t ilen, uint32_t * olen, uint32_t * ohash);	//Создает новую строку из n символов заданной строки, одновременно вычисляя хэш без учета регистра
char *		hashStringCopyCaseN(char * dst, const char * src, uint32_t ilen, uint32_t * olen, uint32_t * ohash);	//Копирует строку из n символов заданной строки, одновременно вычисляя хэш без учета регистра

/*Математические функции*/
uint32_t	randomValue(uint32_t * seed);	//Вычисление псевдо-случайного числа


/*Дата и время*/
void		sleepSeconds(uint32_t sec);			//Усыпляет процесс / поток на sec количество секунд
void		sleepMilliseconds(uint32_t usec);	//Усыпляет процесс / поток на usec количество миллисекунд
void		sleepMicroseconds(uint32_t msec);	//Усыпляет процесс / поток на usec количество микросекунд
string_s *	datetimeFormat(time_t ts, const char * format);	//Возвращает строку, содержащую дату и время согласно заданного формата
uint32_t	nowNanoseconds(void);	//Функция возвращает текущее значение наносекунд


/*Преобразования чисел и строк*/
char * 		intToString(int64_t n, uint32_t * olen);	//Конвертирует INT64 число в строку
uint32_t	intToStringPtr(int64_t n, char * result);	//Конвертирует INT64 число в строку buf
char *		doubleToString(double arg, int ndigits, uint32_t * olen);	//Конвертирует DOUBLE число в строку
uint32_t	doubleToStringPtr(double arg, int ndigits, char * result);	//Конвертирует DOUBLE число в строку
int64_t		stringToInt64(const char *nptr, const char ** rptr);	//Преобразует текстовую строку в число типа int64_t, если задана rptr, в нее записывается позиция, следующая за числом
bool		stringIsInt(const char *p);	//Проверяет, является ли переданная строка числом типа INT
bool		stringIsUnsignedInt(const char *p);	//Проверяет, является ли переданная строка числом типа UNSIGNED INT
bool		stringIsDouble(const char *p);	//Проверяет, является ли переданная строка числом типа DOUBLE


/*Работа со строками*/
char *			stringRandom(uint32_t count, char * str);	//Функция генерирует случайную строковую последовательность (a-zA-Z0-9) запрошенного размера и возвращает указатель на ее начало
uint32_t		stringCopy(char * to, const char * from);	//Функция копирования символов из одной строки в другую 
uint32_t		stringCopyN(char * to, const char * from, uint32_t len);	//Функция копирования определенного количества символов из одной строки в другую
uint32_t		stringCopyCaseN(char * to, const char * from, uint32_t len);	//Функция копирования определенного количества символов из одной строки в другую, при этом текст преобразуется к нижнему регистру
char *			stringClone(const char * from, uint32_t * len);	//Создает новую строку и копирует в нее содержимое строки from
char *			stringCloneN(const char * from, uint32_t ilen, uint32_t * olen);	//Создает новую строку и копирует в нее содержимое ilen символов из строки from
char *			stringCloneCaseN(const char * from, uint32_t ilen, uint32_t * olen);	//Создает новую строку и копирует в нее содержимое ilen символов из строки from, при этом текст преобразуется к нижнему регистру
char *			stringCloneStringS(const_string_s * from, uint32_t * olen);	//Создает новую строку и копирует в нее содержимое структуры const_string_s
uint32_t		stringReverse(char * s, uint32_t ilen);	//Переворачивает N символов строки (зекальное отражение)
const char *	stringExplode(const char **str, u_char delimer, size_t * len);	//Разбивает стоку, используя в качестве разделителя символ delimer

inline bool		stringCompare(const char *str1, const char *str2);	//Сравнивает две строки
inline bool		stringCompareCase(const char *str1, const char *str2);	//Сравнивает две строки без учета регистра
inline bool		stringCompareN(const char *str1, const char *str2, uint32_t len);	//Сравнивает n символов двух строк
inline bool		stringCompareCaseN(const char *str1, const char *str2, uint32_t len);	//Сравнивает n символов двух строк без учета регистра

inline bool		stringIsHex(const char * str, uint32_t count);	//Функция проверяет, является ли строка либо часть строки в количестве символов count записью в HEX формате.
bool			charIsUnreserved(u_char in);	//Проверяет, нявляется ли символ зарезервированным или нет
inline bool		charExists(char c, const char * array);	//Проверяет, находится ли указанный символ в массиве символов
inline bool		charIsZero(char * ptr);	//Проверяет, является ли указатель на текстовую строку нулевым либо значение по данному указателю равно '\0'
inline u_char	charFromHex(const char * str);	//Преобразует HEX представление в символ типа char
inline char *	hexFromChar(u_char n, char * str);	//Преобразует символ типа char в HEX представление
inline u_char	charToLower(u_char ch);	//Приводит символ к нижнему регистру
inline u_char	charToUpper(u_char ch);	//Приводит символ к верхнему регистру
inline char *	charSearchN(const char *str, char c, size_t n);	//Ищет первое вхождение символа c в первых n символах строки str

/*Кодирование и декодирование строки*/
int 			utf8To16(unsigned short *utf16, const char utf8[], int len);	//Преобразует UTF8 в UTF16
void			utf16To8(buffer_s * buf, unsigned short utf16);	//Преобразует UTF16 в UTF8
int32_t			unicodeToUtf8(char * r, uint32_t wc, int32_t n);	//Преобразует числовое значение Юникода в символы UTF 8,16,32

buffer_s *		encodeJson(const char * str, uint32_t ilen, buffer_s * buf);	//Преобразует строку из символьного представления в JSON представление, возвращает новую строку
buffer_s *		decodeJson(const char * str, uint32_t ilen, buffer_s * buf);	//Преобразует строку из JSON представления в символьного представление, возвращает новую строку
buffer_s *		encodeUrlQuery(const char * str, uint32_t ilen, buffer_s * buf);	//Преобразует строку из символьного представления в HEX представление, возвращает новую строку
buffer_s *		decodeUrlQuery(const char * str, uint32_t ilen, buffer_s * buf);	//Декодирует URL Query строку из HEX представления в символьное



/*Работа с файловой системой*/
bool			dirExists(const char * dirname);	//Проверяет существование директории
char *			getCurrentDir(uint32_t * olen);	//Возвращает путь к текущей рабочей директории
bool			fileStat(struct stat * st, const char * filename);	//Получает информацию о файле и записывает ее в структуру struct stat, возвращает false в случае ошибки
bool			fileExists(const char * filename);	//Проверяет существование файла
int64_t 		fileSize(const char * filename);	//Возвращает размер файла
char *			fileRealpath(const char * filename, uint32_t * olen);	//Возвращает полный путь к файлу из относительного
char *			fileRead(const char * filename, int64_t offset, int64_t ilen, int64_t * olen);	//Читает из файла filename ilen байт, начиная с позиции offset
string_s *		pathConcat(string_s * root, string_s * path);	//Объединяет корневую папку и запрошенный путь
char *			pathConcatS(const char * root, const char * path, uint32_t * olen);	//Объединяет корневую папку и запрошенный путь
string_s *		eTag(struct stat * st);	//Вычисляет и возвращает ETag на основании информации о файле struct stat


/*Работа с IP адресами*/
char *			ipToString(socket_addr_s * sa, char * buf);	//Преобразовывает IP адрес из  структуры socket_addr_s в текстовое представление
socket_addr_s *	stringToIp(const char * buf, socket_addr_s * sa);	//Преобразовывает текстовое представление IP адреса в структуруы socket_addr_s
bool			ipCompare(socket_addr_s * ip1, socket_addr_s * ip2);	//Сравнивает два IP адреса и возвращает true если они идентичны


/*Валидация (проверка корректности) данных*/
bool			isValidEmail(const char * email);	//Проверяет корректность адреса электронной почты



/***********************************************************************
 * Функции: core/config.c - Работа с конфигурациями
 **********************************************************************/

void				configReadAll(const char * dir_name);			//Читает все конфигурационные файлы из директории конфигурации
inline bool			configRequireBool(const char * var_name);		//Запрос значения bool переменной, наличие которой обязательно
inline int64_t		configRequireInt(const char * var_name);		//Запрос значения int переменной, наличие которой обязательно
inline double		configRequireDouble(const char * var_name);		//Запрос значения double переменной, наличие которой обязательно
inline const char *	configRequireString(const char * var_name);		//Запрос значения текстовой переменной, наличие которой обязательно

inline bool			configGetBool(const char * var_name, bool def);				//Запрос значения bool переменной
inline int64_t		configGetInt(const char * var_name, int64_t def);			//Запрос значения int переменной
inline double		configGetDouble(const char * var_name, double def);			//Запрос значения double переменной
inline const char *	configGetString(const char * var_name, const char * def);	//Запрос значения текстовой переменной




/***********************************************************************
 * Функции: core/extensions.c - Работа с .so расширениями приложения
 **********************************************************************/

bool				extensionsLoad(void);	//Загрузка расширений
void				extensionsClose(void);	//Закрытие расширений

#ifdef __cplusplus
}
#endif

#endif //_XGCORE_H
