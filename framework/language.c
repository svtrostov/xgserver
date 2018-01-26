/***********************************************************************
 * XG SERVER
 * framework/language.c
 * Работа с языковыми локализациями
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   

#include "core.h"
#include "kv.h"
#include "globals.h"
#include "language.h"


static kv_s	* conf_lang_kv = NULL;				// languages.conf :: /lang
static kv_s	* conf_lang_availables_kv = NULL;	// languages.conf :: /lang/availables
static kv_s	* conf_lang_default_kv = NULL;		// languages.conf :: /lang/default
static bool	conf_lang_loaded	= false;
static const char * conf_lang_default = NULL;	// Языковая локализация, заданная по-умолчанию


static inline void _langInit(void){
	if(conf_lang_loaded) return;
	conf_lang_loaded = true;
	conf_lang_kv =  kvSearch(XG_CONFIG, CONST_STR_COMMA_LEN("lang"));
	if(!conf_lang_kv) return;
	conf_lang_availables_kv = kvSearch(conf_lang_kv, CONST_STR_COMMA_LEN("availables"));
	if(!conf_lang_availables_kv || conf_lang_availables_kv->type != KV_OBJECT || !conf_lang_availables_kv->value.v_list.first){
		conf_lang_availables_kv = NULL;
		return;
	}
	conf_lang_default_kv = kvSearch(conf_lang_kv, CONST_STR_COMMA_LEN("default"));
	if(!conf_lang_default_kv || conf_lang_default_kv->type != KV_STRING || kvSearch(conf_lang_availables_kv, conf_lang_default_kv->value.v_string.ptr, conf_lang_default_kv->value.v_string.len) == NULL){
		kv_s * kv = conf_lang_availables_kv->value.v_list.first;
		if(kv->key_name != NULL && kv->key_len > 0){
			conf_lang_default_kv = kvSetString(kvNew(), kv->key_name, kv->key_len);
		}
	}

	conf_lang_default = (const char *)conf_lang_default_kv->value.v_string.ptr;

}//END: _langInit


/***********************************************************************
 * Функции пользователь <--> сессия
 **********************************************************************/

/*
 * Проверяет существование выбранной языковой локализации
 * language - название языковой локали
 * language_n - количество символов (длинна строки) language
 */
bool
langExists(const char * language, uint32_t language_n){
	_langInit();
	if(!conf_lang_availables_kv) return false;
	return (kvSearch(conf_lang_availables_kv, language, language_n) != NULL);
}//END: langExists



/*
 * Возвращает языковую локализацию, установленную по-умолчанию
 */
const char *
langDefault(void){
	_langInit();
	return conf_lang_default;
}//END: langDefault



/*
 * Возвращает языковую локализацию, которую можно исползовать
 */
const char *
langSelect(const char * language, uint32_t language_n){
	_langInit();
	if(!language || !*language || !langExists(language,language_n)) language = langDefault();
	if(!language || !*language) language = LANG_DEFAULT;
	return language;
}//END: langSelect




/*
 * Возвращает значение 
 */
const char *
langGetValue(const char * path, const char * language, const char * def){
	_langInit();
	if(!conf_lang_kv || !path || !language) return def;
	kv_s * path_kv = kvGetByPath(conf_lang_kv, path);
	if(!path_kv) return def;
	kv_s * locale_kv = kvSearch(path_kv, language, 0);
	if(!locale_kv || locale_kv->type != KV_STRING || !locale_kv->value.v_string.ptr) return def;
	return (const char *) locale_kv->value.v_string.ptr;
}//END: lang


/*
 * Возвращает значение 
 */
const char *
langGetValueN(const char * path, const char * language, const char * def, uint32_t * olen){
	_langInit();
	if(!conf_lang_kv || !path || !language) goto label_def;
	kv_s * path_kv = kvGetByPath(conf_lang_kv, path);
	if(!path_kv) goto label_def;
	kv_s * locale_kv = kvSearch(path_kv, language, 0);
	if(!locale_kv || locale_kv->type != KV_STRING || !locale_kv->value.v_string.ptr) goto label_def;
	if(olen) *olen = locale_kv->value.v_string.len;
	return (const char *) locale_kv->value.v_string.ptr;

	label_def:
	if(olen) *olen = (def ? strlen(def) : 0);
	return (const char *)def;
}//END: lang
