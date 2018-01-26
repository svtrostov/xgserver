/***********************************************************************
 * XG SERVER
 * core/extensions.c
 * Работа с .so расширениями приложения
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include <stdio.h>
#include <dlfcn.h>
#include "core.h"
#include "event.h"
#include "globals.h"
#include "kv.h"




static string_s extensions_path;
static extensions_s  extensions;


/*
 * Добавление успешно загруженного расширения в список расширений
 */
static void
_extensionAddToExtensionsList(extension_s * ext){
	if(extensions.last) extensions.last->next = ext;
	if(!extensions.first) extensions.first = ext;
	extensions.last = ext;
	extensions.count++;
}//END: _extensionAddToExtensionsList


/*
 * Подключение расширения из файла
 */
static bool
_extensionLoad(extension_s * ext){
	char *error;
	ext->handle = dlopen(ext->filename, RTLD_LAZY);
	if(!ext->handle) RETURN_ERROR(false, "dlopen() error: %s", dlerror());
	int (*extexec)(extension_s * );
	extexec = dlsym(ext->handle, "start");
	if ((error = dlerror()) != NULL){
		DEBUG_MSG("dlsym() error: %s", error);
		dlclose(ext->handle);
		return false;
	}
	int code;
	if((code = (*extexec)(ext)) != 0){
		DEBUG_MSG("WARNING: internal error [%d] in start function of extension: %s", code, ext->filename);
		dlclose(ext->handle);
		return false;
	}

	return true;
}//END: _extensionLoad



/*
 * Закрытие расширения
 */
static bool
_extensionClose(extension_s * ext){
	if(!ext || !ext->handle) return false;
	dlclose(ext->handle);
	if(ext->filename) mFree(ext->filename);
	mFree(ext);
	return true;
}//END: _extensionClose




/***********************************************************************
 * Функции
 **********************************************************************/


/*
 * Загрузка расширений
 */
bool
extensionsLoad(void){

	memset(&extensions, '\0', sizeof(extensions_s));

	//Получение настроек расширений из конфигурационного файла
	kv_s * extconfig = kvGetByPath(XG_CONFIG,"/extensions");
	kv_s * extlist = kvGetByPath(extconfig,"list");
	if(!extconfig) RETURN_ERROR(false,"WARNING: extensions config [/extensions] not found");
	if(!extlist) RETURN_ERROR(false,"WARNING: variable [/extensions/list] not found");
	if(extlist->type != KV_OBJECT) RETURN_ERROR(false,"WARNING: variable [/extensions/list] is not object");
	extensions_path.ptr = fileRealpath(kvGetStringByPath(extconfig,"path",NULL), &extensions_path.len);
	if(!extensions_path.ptr) RETURN_ERROR(false,"WARNING: extensions path [/extensions/path] not found");
	struct stat st;
	if(stat(extensions_path.ptr, &st)!=0) RETURN_ERROR(false,"Can not get info about [%s] directory\n", extensions_path.ptr);
	if(!S_ISDIR(st.st_mode)) RETURN_ERROR(false,"[%s] is not a directory\n", extensions_path.ptr);
	if(access(extensions_path.ptr, R_OK)==-1) RETURN_ERROR(false,"Can not read from directory [%s]\n", extensions_path.ptr);

	//Просмотр списка зарегистрированных расширений
	extension_s * extension;
	char * filename = NULL;
	kv_s * extfile = extlist->value.v_list.first;
	while(extfile){
		if(extfile->type != KV_BOOL || !extfile->value.v_bool || !extfile->key_name || extfile->key_len < 4 /*x.so*/) goto label_continue;
		if(filename) mFree(filename);
		filename = pathConcatS(extensions_path.ptr, extfile->key_name, NULL);

		//Файл не найден
		if(!fileStat(&st,filename)){
			DEBUG_MSG("WARNING: extension [%s] file [%s] not found", extfile->key_name, filename);
			goto label_continue;
		}

		extension = (extension_s *)mNewZ(sizeof(extension_s));
		extension->filename = filename;

		//Подключение расширения из файла
		if(!_extensionLoad(extension)){
			mFree(extension);
		}else{
			_extensionAddToExtensionsList(extension);
			filename = NULL;
		}

		label_continue:
		extfile = extfile->next;
	}//Просмотр списка зарегистрированных расширений

	if(filename) mFree(filename);
	return true;
}//END: extensionsLoad



/*
 * Закрытие расширений
 */
void
extensionsClose(void){
	extension_s * ext_current;
	extension_s * ext_next = extensions.first;
	while(ext_next){
		ext_current = ext_next;
		ext_next = ext_current->next;
		_extensionClose(ext_current);
	}
}//END: extensionsClose





















