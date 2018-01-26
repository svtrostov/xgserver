/***********************************************************************
 * XG SERVER
 * core/filecache.h
 * Хэш таблица
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   

#include "core.h"
#include "kv.h"
#include "globals.h"
#include "filecache.h"
#include "server.h"


//Кеш файлов
static filecache_s * filecache = NULL;

//Мьютекс синхронизации в момент добавления / обновления файла в кеше
static pthread_mutex_t filecache_mutex = PTHREAD_MUTEX_INITIALIZER;



/***********************************************************************
 * Функции
 **********************************************************************/



/*
 * Инициализация кеша файлов
 */
void
filecacheInit(void){

	if(filecache) return;
	filecache = (filecache_s * )mNewZ(sizeof(filecache_s));

	filecache->cache = kvNewRoot();

	filecache->max_filesize	= (uint32_t)configGetInt("/filecache/max_size", cache_default_max_file_size);	//Максимальный размер файлов, добавляемых в кеш
	filecache->cache_limit	= (uint32_t)configGetInt("/filecache/max_files", cache_default_max_file_count);	//Максимальное количество файлов в кеше

}//END: filecacheInit



static void 
filecacheFileFree(void * ptr){
	static_file_s * sf = (static_file_s *)ptr;
	sf->in_cache = false;
	requestStaticFileFree(sf);
}//END: filecacheFileFree



/*
 * Обновляет кеш файла и возвращает указатель на обновленную структуру static_file_s или NULL, если обновление не удалось
 */
static kv_s *
filecacheUpdate(kv_s * fnode, static_file_s * sf){

	static_file_s * fcache = (static_file_s *)&fnode->value.v_pointer;

	//Статистика кешированного файла и запрошенного файла совпадают - ничего не делаем
	if(memcmp(&fcache->st, &sf->st, sizeof(struct stat))==0) return fnode;

	//Если новый размер файла больше установленного лимита -> удаляем файл из кеша
	if(sf->st.st_size > filecache->max_filesize){
		kvFree(kvRemove(fnode));
		filecache->cache_count--;
		return NULL;
	}

	buffer_s * content = bufferLoadFromFile(sf->localfile->ptr, NULL);
	if(!content){
		kvFree(kvRemove(fnode));
		filecache->cache_count--;
		return NULL;
	}

	fcache = sf;
	kvSetPointer(fnode, sf, filecacheFileFree);
	sf->in_cache	= true;
	sf->content		= content;

	return fnode;
}//filecacheUpdate



/*
 * Добавляет файл в кеш и возвращает указатель на добавленную структуру kv_s или NULL, если добавление не удалось
 */
static kv_s *
filecacheAdd(static_file_s * sf){
	if(sf->st.st_size > filecache->max_filesize) return NULL;	//Слишком большой файл для кеша
	if(filecache->cache_count >= filecache->cache_limit) return NULL;	//Достигнут лимит количества файлов в кеше

	buffer_s * content = bufferLoadFromFile(sf->localfile->ptr, NULL);
	if(!content) return NULL;

	kv_s * node		= kvSetPointerByPath(filecache->cache, sf->uri.ptr, sf, filecacheFileFree);
	sf->in_cache	= true;
	sf->content		= content;

	filecache->cache_count++;

	return node;
}//filecacheAdd




/*
 * Возвращает файл из кеша, если файла в кеше нет - добавляет его в кеш, или возвращает NULL, если по каким-либо причинам не удалось добавить файл в кеш
 */
static_file_s *
filecacheGet(static_file_s * sf){

	if(!sf || !sf->uri.ptr) return NULL;
	static_file_s * result = NULL;

	pthread_mutex_lock(&filecache_mutex);
	kv_s * fnode = kvGetByPath(filecache->cache, sf->uri.ptr);
	//Файла нет в кеше - пытаемся добавить
	if(!fnode){
		fnode = filecacheAdd(sf);
		if(fnode == NULL) result = NULL;
		else result = (static_file_s *)fnode->value.v_pointer.ptr;
	}
	//Если файл есть в кеше, то при необходимости обновляем кеш
	else{
		//fnode = filecacheUpdate(fnode, sf);
		if(fnode == NULL) result = NULL;
		else result = (static_file_s *)fnode->value.v_pointer.ptr;
	}
	pthread_mutex_unlock(&filecache_mutex);

	return result;
}//END: filecacheGet













