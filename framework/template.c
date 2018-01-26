/***********************************************************************
 * XG SERVER
 * framework/template.c
 * Работа с HTML шаблонами
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   

#include "core.h"
#include "kv.h"
#include "globals.h"
#include "language.h"
#include "template.h"


//Мьютекс синхронизации в момент обращения к IDLE списку
static pthread_mutex_t template_idle_mutex = PTHREAD_MUTEX_INITIALIZER;
static template_s * _template_idle_list = NULL;


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
_toIdle(_templateToIdle, template_s, _template_idle_list, template_idle_mutex);

//Получает элемент из IDLE списка или создает новый, если IDLE список пуст
_fromIdle(_templateFromIdle, template_s, _template_idle_list, template_idle_mutex);



/**
 * Инициализация template.c
 */
initialization(template_c){
	int i;
	for(i=0;i<256;i++) _templateToIdle(NULL);
	DEBUG_MSG("template.c initialized.");
}//END: initialization



/***********************************************************************
 * Функции шаблонизатора
 **********************************************************************/


/*
 * Создает новый шаблонизатор
 */
template_s *
templateCreate(const char * filename, const char * path){
	template_s * template = _templateFromIdle();
	if(filename) templateFromFile(template, filename, path);
	template->binds = kvNewRoot();
	return template;
}//END: templateCreate



/*
 * Освобождает память, занятую шаблонизатором
 */
void
templateFree(void * ptr){
	template_s * template = (template_s *)ptr;
	if(template->content) bufferFree(template->content);
	if(template->binds) kvFree(template->binds);
	_templateToIdle(template);
}//END: templateFree



/*
 * Загрузка шаблона из указанного файла
 */
bool
templateFromFile(template_s * template, const char * filename, const char * path){
	if(!template || !filename) return false;
	if(template->content) bufferFree(template->content);
	template->content = bufferLoadFromFile(filename, path);
	return (template->content ? true : false);
}//END: templateFromFile



/*
 * Загрузка шаблона из строки
 */
bool
templateFromString(template_s * template, const char * src, uint32_t ilen){
	if(!template || !src) return false;
	if(!template->content) template->content = bufferCreate(0); else bufferClear(template->content);
	bufferAddStringN(template->content, src, ilen);
	return true;
}//END: templateFromString



/*
 * Загрузка шаблона из строки string_s
 */
bool
templateFromStringS(template_s * template, const_string_s * src){
	if(!template || !src || !src->ptr || !src->len) return false;
	if(!template->content) template->content = bufferCreate(0); else bufferClear(template->content);
	bufferAddStringN(template->content, src->ptr, src->len);
	return true;
}//END: templateFromStringS



/*
 * Загрузка шаблона из буфера buffer_s
 */
bool
templateFromBuffer(template_s * template, buffer_s * buf){
	if(!template || !buf) return false;
	if(!template->content) template->content = bufferCreate(0); else bufferClear(template->content);
	bufferAddStringN(template->content, buf->buffer, buf->count);
	return true;
}//END: templateFromBuffer



/*
 * Возвращает указатель на структуру kv_s списка макроподстановок
 */
kv_s *
templateGetBinds(template_s * template){
	return template->binds;
}//END: templateGetBinds



inline kv_s *
templateBindNull(template_s * template, const char * path){
	if(!path || !*path) return NULL;
	return kvSetNull(kvSetByPath(template->binds, path));
}

inline kv_s *
templateBindBool(template_s * template, const char * path, bool value){
	if(!path || !*path) return NULL;
	return kvSetBool(kvSetByPath(template->binds, path), value);
}

inline kv_s *
templateBindInt64(template_s * template, const char * path, int64_t value){
	if(!path || !*path) return NULL;
	return kvSetInt(kvSetByPath(template->binds, path), (int64_t)value);
}

inline kv_s *
templateBindUInt64(template_s * template, const char * path, uint64_t value){
	if(!path || !*path) return NULL;
	return kvSetInt(kvSetByPath(template->binds, path), (int64_t)value);
}

inline kv_s *
templateBindInt32(template_s * template, const char * path, int32_t value){
	if(!path || !*path) return NULL;
	return kvSetInt(kvSetByPath(template->binds, path), (int64_t)value);
}

inline kv_s *
templateBindUInt32(template_s * template, const char * path, uint32_t value){
	if(!path || !*path) return NULL;
	return kvSetInt(kvSetByPath(template->binds, path), (int64_t)value);
}


inline kv_s *
templateBindDouble(template_s * template, const char * path, double value){
	if(!path || !*path) return NULL;
	return kvSetDouble(kvSetByPath(template->binds, path), value);
}

inline kv_s *
templateBindString(template_s * template, const char * path, const char * str, uint32_t len){
	if(!path || !*path) return NULL;
	return kvSetString(kvSetByPath(template->binds, path), str, len);
}

inline kv_s *
templateBindStringPtr(template_s * template, const char * path, char * str, uint32_t len){
	if(!path || !*path) return NULL;
	return kvSetStringPtr(kvSetByPath(template->binds, path), str, len);
}

inline kv_s *
templateBindDatetime(template_s * template, const char * path, time_t time, const char * format){
	if(!path || !*path) return NULL;
	return kvSetDatetime(kvSetByPath(template->binds, path), time, format);
}



/***********************************************************************
 * Обработка шаблона
 **********************************************************************/

typedef enum{
	MT_IS_UNDEFINED,
	MT_IS_VARIABLE,
	MT_IS_LANG,
	MT_IS_CONF
} macro_type;

#define _skip(p,n) do{p+=n; while(*p && isspace((int)*p))p++; if(!*p) goto label_template_end;}while(0)

/*
 * Парсинг шаблона и замена макроподстановок заданными значениями
 * Общий вид макроподстановки в шаблоне: {[variable]}
 * Примеры:
 * {[test]} или {[/test]} или {[xxx/var1]} или {[/xxx/var1]} - заменяет макроподстановку значением ключа test (var1 из xxx) взятую из template->binds
 * Макроподстановки, начинающиеся с символа @, считаются функциями
 * {[@lang:/users/login/form/title]} - заменяет макроподстановку текстом в указанной локалиации
 * {[@conf:/webserver/host]} - заменяет макроподстановку значением переменной конфигурации
 * 
 * Если через @lang или @conf задан путь к объекту {} или массиву [], то вывод объекта или массива будет в формате JSON 
 */
buffer_s *
templateParse(template_s * template, const char * language){

	if(!template || !template->content) return NULL;
	buffer_s * buf = bufferCreate(0);
	language = langSelect(language, 0);
	const char * begin = (const char *)template->content->buffer;
	const char * ptr = begin;
	const char * value;
	uint32_t	value_n;
	macro_type	type = MT_IS_UNDEFINED;
	kv_s * kv;
	char tmp[256];

	while(*ptr){

		//Найдена макроподстановка
		if(*ptr == '{' && *(ptr+1) == '['){

			bufferAddStringN(buf, begin, (uint32_t)(ptr-begin));
			begin = NULL;

			_skip(ptr,2);

			//Найдена функция
			if(*ptr == '@'){
				type = MT_IS_UNDEFINED;
				_skip(ptr,1);
				//Вставка текста согласно языковой локали 
				if(stringCompareCaseN(ptr,"lang:",5)){
					_skip(ptr,5);
					type = MT_IS_LANG;
				}else
				if(stringCompareCaseN(ptr,"conf:",5)){
					_skip(ptr,5);
					type = MT_IS_CONF;
				}
				
			}
			//Найдена переменная
			else{
				type = MT_IS_VARIABLE;
			}

			value = ptr;
			while(*ptr && (*ptr != ']' || *(ptr+1) != '}')) ptr++;
			if(!*ptr) goto label_template_end;
			value_n = ptr - value;
			ptr+=2;

			begin = ptr;

			while(value_n>0 && isspace((int)value[value_n-1])) value_n--;
			if(!value_n || value_n > 255) continue;

			switch(type){

				//Вставка значения переменной из template->binds
				case MT_IS_VARIABLE:
					stringCopyN(tmp, value, value_n);
					kv = kvGetByPath(template->binds, tmp);
					if(kv) kvAsString(kv, buf);
				break;

				//Вставка строки заданной языковой локализации
				case MT_IS_LANG:
					stringCopyN(tmp, value, value_n);
					//printf("MT_IS_LANG (%s): [%s]\n",language, tmp);
					value = langGetValueN(tmp, language, NULL, &value_n);
					if(value) bufferAddStringN(buf, value, value_n);
				break;

				//Вставка значения переменной из конфигурационных файлов
				case MT_IS_CONF:
					stringCopyN(tmp, value, value_n);
					kv = kvGetByPath(XG_CONFIG, tmp);
					if(kv) kvAsString(kv, buf);
				break;

				case MT_IS_UNDEFINED:
				default:
				break;
			}

			continue;
		}//Найдена макроподстановка

		ptr++;
	}//while

	label_template_end:

	if(begin != NULL && ptr > begin) bufferAddStringN(buf, begin, (uint32_t)(ptr-begin));

	return buf;
}//END: templateParse

#undef _skip











