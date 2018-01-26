/***********************************************************************
 * XG SERVER
 * core/memory.c
 * Ядро: Работа с памятью
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include "core.h"


#ifdef XG_MEM_USE_CACHE


//Мьютекс синхронизации в момент работы с IDLE списками
static pthread_mutex_t mem_idle_mutex = PTHREAD_MUTEX_INITIALIZER;

//Размер минимального блока, выделяемого malloc
static size_t malloc_min_size = 0;

//Размер инкремента malloc
static size_t malloc_increment_size = 0;


typedef struct{
	void		**list;	//Список указателей на выделенные блоки памяти
	uint32_t	count;	//Количество доступных блоков памяти
	uint32_t	size;	//Размер списка
	uint32_t	min;	//Минимальный объем списка
	uint32_t	uses;	//Количество использований
} _idle_list;


//Мы будем кешировать следующие блоки данных: 
static _idle_list idle_list[4] = {
	{NULL,	0,	4096	,0,0},	//до malloc_min_size включительно (24/32 байта на x64 или 16 байт на х86)
	{NULL,	0,	2048	,0,0},	//до malloc_min_size + malloc_increment_size включительно
	{NULL,	0,	1024	,0,0},	//до malloc_min_size + malloc_increment_size*2 включительно
	{NULL,	0,	128		,0,0}	//равный malloc_1024_size
};

//Размеры malloc
static size_t malloc_incrementx1_size = 0;
static size_t malloc_incrementx2_size = 0;
static size_t malloc_1024_size = 0;

static bool mem_cache_on = false;



//Получение блока памяти из кеша или выделение памяти
static void * _getFromIdle(size_t bytes){
	if(!mem_cache_on) return malloc(bytes);
	_idle_list * idle = NULL;
	void * ptr = NULL;
	if(bytes <= malloc_min_size) idle = &idle_list[0];
	else if(bytes <= malloc_incrementx1_size) idle = &idle_list[1];
	else if(bytes <= malloc_incrementx2_size) idle = &idle_list[2];
	else if(bytes == 1024) idle = &idle_list[3];
	if(idle){
		pthread_mutex_lock(&mem_idle_mutex);
		if(idle->count > 0){
			idle->count--;
			ptr = idle->list[idle->count];
			idle->list[idle->count] = NULL;
			#ifdef XG_MEMSTAT
			if(idle->count < idle->min) idle->min = idle->count;
			idle->uses++;
			#endif
		}
		pthread_mutex_unlock(&mem_idle_mutex);
	}
	if(!ptr) ptr = malloc(bytes);
	return ptr;
}


//Добавление блока памяти в кеш или освобождение памяти
static void _setToIdle(void * ptr){
	if(!ptr) return;
	if(!mem_cache_on){
		free(ptr);
		return;
	}
	_idle_list * idle = NULL;
	size_t bytes = malloc_usable_size(ptr);
	if(!bytes){
		ERROR_MSG("Incorrect pointer for free: %p",ptr);
		return;
	}
	if(bytes <= malloc_min_size) idle = &idle_list[0];
	else if(bytes <= malloc_incrementx1_size) idle = &idle_list[1];
	else if(bytes <= malloc_incrementx2_size) idle = &idle_list[2];
	else if(bytes == malloc_1024_size) idle = &idle_list[3];
	if(idle){
		pthread_mutex_lock(&mem_idle_mutex);
		if(idle->count < idle->size){
			idle->list[idle->count] = ptr;
			idle->count++;
			ptr = NULL;
		}
		pthread_mutex_unlock(&mem_idle_mutex);
	}
	if(ptr) free(ptr);
}


void mCacheOn(void){mem_cache_on = true;}
void mCacheOff(void){mem_cache_on = false;}


#endif



#ifdef XG_MEMSTAT

//Мьютекс синхронизации в момент сбора статистики
static pthread_mutex_t mem_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool		mem_stat		= false;
static uint32_t malloc_count	= 0;
static uint32_t free_count		= 0;



void
mStatStart(void){
	pthread_mutex_lock(&mem_mutex);
	mem_stat = true;
	printf("mStatStart()...\n");
	pthread_mutex_unlock(&mem_mutex);
}

void
mStatStop(void){
	pthread_mutex_lock(&mem_mutex);
	mem_stat = false;
	printf("mStatStop()...\n");
	pthread_mutex_unlock(&mem_mutex);
}

void
mStatPrint(void){
	pthread_mutex_lock(&mem_mutex);
	printf(
		"\n-----------------------\nMem stat on time: [%u]\n"\
		"Malloc count = [%u]\n"\
		"Free count = [%u]\n"\
		"Difference = [%u]\n"
#ifdef XG_MEM_USE_CACHE
		"Cache info:\n"\
		"\t count 0 = [%u] of [%u] -> min [%u] uses [%u]\n"\
		"\t count 1 = [%u] of [%u] -> min [%u] uses [%u]\n"\
		"\t count 2 = [%u] of [%u] -> min [%u] uses [%u]\n"\
		"\t count 3 = [%u] of [%u] -> min [%u] uses [%u]\n",
#else
,
#endif
		(uint32_t)time(NULL), 
		malloc_count, 
		free_count, 
		malloc_count-free_count
#ifdef XG_MEM_USE_CACHE
		,idle_list[0].count,idle_list[0].size,idle_list[0].min,idle_list[0].uses
		,idle_list[1].count,idle_list[1].size,idle_list[1].min,idle_list[1].uses
		,idle_list[2].count,idle_list[2].size,idle_list[2].min,idle_list[2].uses
		,idle_list[3].count,idle_list[3].size,idle_list[3].min,idle_list[3].uses
#endif
	);
	pthread_mutex_unlock(&mem_mutex);
}

#ifdef XG_MEMSTAT_BACKTRACE
#include <execinfo.h>
static void
_mBacktrace(void){
	void * array[10];
	size_t size;
	char ** strings;
	size_t i;
	size = backtrace (array, 10);
	strings = backtrace_symbols (array, size);
	for (i = 0; i < size; i++)	printf ("\t\t%s\n", strings[i]);
	//if(size>0) printf ("%s\n", strings[1]);
	free(strings);
}
#endif
#endif







/**
 * Инициализация memory.c
 */
initialization(memory_c){

#ifdef XG_MEM_USE_CACHE
	char * test = malloc(1);
	malloc_min_size = malloc_usable_size(test);	//Размер минимального блока, выделяемого malloc
	free(test);
	test = malloc(malloc_min_size + 1);
	malloc_increment_size = malloc_usable_size(test) - malloc_min_size;	//Размер инкремента malloc
	free(test);
	test = malloc(1024);
	malloc_1024_size = malloc_usable_size(test);
	free(test);

	malloc_incrementx1_size	= malloc_min_size + malloc_increment_size;
	malloc_incrementx2_size	= malloc_min_size + malloc_increment_size * 2;

	int i;
	idle_list[0].list = calloc(idle_list[0].size, sizeof(void*));
	for(i=0;i<idle_list[0].size;i++) idle_list[0].list[i] = malloc(malloc_min_size);
	idle_list[0].min = idle_list[0].count = idle_list[0].size;

	idle_list[1].list = calloc(idle_list[1].size, sizeof(void*));
	for(i=0;i<idle_list[1].size;i++) idle_list[1].list[i] = malloc(malloc_incrementx1_size);
	idle_list[1].min = idle_list[1].count = idle_list[1].size;

	idle_list[2].list = calloc(idle_list[2].size, sizeof(void*));
	for(i=0;i<idle_list[2].size;i++) idle_list[2].list[i] = malloc(malloc_incrementx2_size);
	idle_list[2].min = idle_list[2].count = idle_list[2].size;

	idle_list[3].list = calloc(idle_list[3].size, sizeof(void*));
	for(i=0;i<idle_list[3].size;i++) idle_list[3].list[i] = malloc(malloc_1024_size);
	idle_list[3].min = idle_list[3].count = idle_list[3].size;

#endif

	DEBUG_MSG("memory.c initialized.");
}//END: initialization


/***********************************************************************
 * Функции - Работа с блоками памяти
 **********************************************************************/


/*
 * Заполнение size байт блока памяти нулями, начиная с ptr 
 */
inline void *
mZero(void * ptr, size_t size){
	return (!ptr ? NULL : memset(ptr,'\0',size));
}



/*
 * Выделение блока памяти malloc
 */
inline void *
mNew(size_t size){
#ifdef XG_MEM_USE_CACHE
	void * ptr = _getFromIdle(size);
#else
	void * ptr = malloc(size);
#endif
	if(!ptr) FATAL_ERROR("Out of memory. Malloc requested %zu bytes.", size);
#ifdef XG_MEMSTAT
	pthread_mutex_lock(&mem_mutex);
	if(mem_stat){
		size = malloc_usable_size(ptr);
		malloc_count++;
		else malloc_more_count++;
#ifdef XG_MEMSTAT_BACKTRACE
		printf("malloc: %u from ->\n",(uint32_t)size);
		_mBacktrace();
#endif
	}
	pthread_mutex_unlock(&mem_mutex);
#endif
	return ptr;
}



/*
 * Выделение блока памяти malloc c обнулением выделенного диапазона
 */
inline void *
mNewZ(size_t size){
	return mZero(mNew(size), size);
}



/*
 * Выделение блока памяти calloc
 */
inline void *
mCalloc(size_t count, size_t size){
#ifdef XG_MEM_USE_CACHE
	size *= count;
	void * ptr = mZero(_getFromIdle(size), size);
#else
	void * ptr = calloc(count, size);
#endif
	if(!ptr) FATAL_ERROR("Out of memory. Сalloc requested %zu bytes.", count * size);
#ifdef XG_MEMSTAT
	pthread_mutex_lock(&mem_mutex);
	if(mem_stat){
		malloc_count++;
	}
	pthread_mutex_unlock(&mem_mutex);
#endif
	return ptr;
}



/*
 * Изменение размера блока памяти realloc
 */
inline void *
mRealloc(void * ptr, size_t size){
	ptr = realloc(ptr, size);
	if(!ptr) FATAL_ERROR("Out of memory. Realloc requested %zu bytes.", size);
	return ptr;
}



/*
 * Изменение блока размера памяти realloc
 */
inline void *
mResize(void * ptr, size_t size){
	return (!ptr ? mNew(size) : mRealloc(ptr, size));
}



/*
 * Освобождение блока памяти free
 */
inline void
mFree(void * ptr){
	if(ptr){
#ifdef XG_MEMSTAT
	size_t size = malloc_usable_size(ptr);
#endif
#ifdef XG_MEM_USE_CACHE
	_setToIdle(ptr);
#else
	free(ptr);
#endif
#ifdef XG_MEMSTAT
	pthread_mutex_lock(&mem_mutex);
		if(mem_stat){
			free_count++;
		}
	pthread_mutex_unlock(&mem_mutex);
#endif
	}
}


/*
 * Освобождение блока памяти free, установка ptr в NULL
 */
inline void
mFreeAndNull(void ** ptr){
	if(ptr){
		if(*ptr) mFree(*ptr);
		*ptr = NULL;
	}
}



/***********************************************************************
 * Функции - Работа со string_s
 **********************************************************************/


/*
 * Создание новой структуры string_s
 */
inline string_s *
mStringNew(void){
	return (string_s *)mNewZ(sizeof(string_s));
}



/*
 * Очистка структуры string_s
 */
inline string_s * 
mStringClear(string_s * str){
	if(str){
		if(str->ptr != NULL) mFree(str->ptr);
		str->ptr = NULL;
		str->len = 0;
	}
	return str;
}



/*
 * Освобождение памяти free для структуры string_s
 */
inline void
mStringFree(string_s * str){
	if(str){
		if(str->ptr != NULL) mFree(str->ptr);
		mFree(str);
	}
	return;
}



/*
 * Освобождение памяти free для структуры string_s, установка str в NULL
 */
inline void
mStringFreeAndNull(string_s ** str){
	if(str){
		mStringFree(*str);
		*str = NULL;
	}
	return;
}


