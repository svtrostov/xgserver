/***********************************************************************
 * XG SERVER
 * core/config.c
 * Работа с конфигурациями
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/  


#include "core.h"
#include "kv.h"
#include "globals.h"

/***********************************************************************
 * Функции
 **********************************************************************/


/*
 * Читает все конфигурационные файлы из директории конфигурации
 */
void
configReadAll(const char * dir_name){
	DIR * d = opendir(dir_name);
	size_t len;
	int path_length;
	char path[PATH_MAX];
	char config_name[PATH_MAX];
	char config_file[PATH_MAX];

	DEBUG_MSG("\n\n------------------------------------\nREAD CONFIG FILES FROM: %s\n------------------------------------\n", realpath(dir_name,path));

	if (!d) return;

	while(1){
		kv_s * config;
		struct dirent * entry;
		const char * d_name;
		entry = readdir (d);
		if (! entry) break;
		d_name = entry->d_name;

		if (!(entry->d_type & DT_DIR)){
			len = strlen(d_name);
			if(len > 5 && stringCompare(d_name + len - 5, ".conf")){
				stringCopyCaseN(config_name, d_name, len - 5);
				snprintf(config_file, PATH_MAX-1, "%s/%s", dir_name, d_name);
				DEBUG_MSG("%s/%s [%s]", dir_name, d_name, d_name);
				config = kvFromJsonFile(config_file, KVJF_ALLOW_ALL);
				if(config){
					kvMerge(XG_CONFIG, config, KV_REPLACE);
				}

			}
		}

		if (entry->d_type & DT_DIR) {       
			if (strcmp (d_name, "..") != 0 &&
				strcmp (d_name, ".") != 0) {
				path_length = snprintf (path, PATH_MAX-1, "%s/%s", dir_name, d_name);
				if (path_length < PATH_MAX-1) configReadAll(path);
			}
		}
	}
	closedir(d);
	return;
}

inline bool configRequireBool(const char * var_name){return kvGetRequireBool(XG_CONFIG, var_name);}	//Запрос значения bool переменной, наличие которой обязательно
inline int64_t configRequireInt(const char * var_name){return kvGetRequireInt(XG_CONFIG, var_name);}	//Запрос значения int переменной, наличие которой обязательно
inline double configRequireDouble(const char * var_name){return kvGetRequireDouble(XG_CONFIG, var_name);}	//Запрос значения double переменной, наличие которой обязательно
inline const char * configRequireString(const char * var_name){return kvGetRequireString(XG_CONFIG, var_name);}	//Запрос значения текстовой переменной, наличие которой обязательно

inline bool configGetBool(const char * var_name, bool def){return kvGetBoolByPath(XG_CONFIG, var_name, def);}	//Запрос значения bool переменной
inline int64_t configGetInt(const char * var_name, int64_t def){return kvGetIntByPath(XG_CONFIG, var_name, def);}	//Запрос значения int переменной
inline double configGetDouble(const char * var_name, double def){return kvGetIntByPath(XG_CONFIG, var_name, def);}	//Запрос значения double переменной
inline const char * configGetString(const char * var_name, const char * def){return kvGetStringByPath(XG_CONFIG, var_name, def);}	//Запрос значения текстовой переменной

