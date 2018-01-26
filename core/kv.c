/***********************************************************************
 * XG SERVER
 * core/kv.c
 * Работа с Key -> Value
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/

#include <math.h>
#include "core.h"
#include "kv.h"

//Мьютекс синхронизации KV
static pthread_mutex_t kv_mutex = PTHREAD_MUTEX_INITIALIZER;

static kv_s * _kv_idle_list = NULL;


/*
 * Добавляет новый/существующмй элемент в IDLE список
 */
static void
_kvToIdle(kv_s * item){
	if(!item) item = (kv_s *)mNewZ(sizeof(kv_s));
	pthread_mutex_lock(&kv_mutex);
		item->next = _kv_idle_list;
		_kv_idle_list = item;
	pthread_mutex_unlock(&kv_mutex);
}//END: _kvToIdle



/*
 * Получает элемент из IDLE списка или создает новый, если IDLE список пуст
 */
static kv_s *
_kvFromIdle(void){
	kv_s * item = NULL;
	pthread_mutex_lock(&kv_mutex);
		if(_kv_idle_list){
			item = _kv_idle_list;
			_kv_idle_list = item->next;
		}
	pthread_mutex_unlock(&kv_mutex);
	if(!item){
		item = (kv_s *)mNewZ(sizeof(kv_s));
	}else{
		memset(item,'\0',sizeof(kv_s));
	}
	return item;
}//END: _kvFromIdle



/**
 * Инициализация kv.c
 */
initialization(kv_c){
	int i;
	for(i=0;i<kv_idle_list_size;i++) _kvToIdle(NULL);
	DEBUG_MSG("kv.c initialized.");
}//END: initialization





/***********************************************************************
 * Работа с KV
 **********************************************************************/


/*
 * Создание KV
 */
inline kv_s *
kvNew(void){
	return _kvFromIdle();
}//END: kvNew



/*
 * Создание KV - рут элемент
 */
inline kv_s *
kvNewRoot(void){
	kv_s * root	= kvNew();
	root->type	= KV_OBJECT;
	return root;
}//END: kvNewRoot



/*
 * Очистка значения KV
 */
kv_s *
kvClear(kv_s * node){
	if(node==NULL) return NULL;
	switch(node->type){
		case KV_ARRAY:
		case KV_OBJECT:
			while(node->value.v_list.first){
				//Если дочерний элемент привязан к другому родителю
				if(node->value.v_list.first->parent != node){
					node->value.v_list.first = NULL;
					node->value.v_list.last = NULL;
					RETURN_ERROR(node, "node->value.v_list.first->parent [%s] != node [%s]",node->value.v_list.first->key_name,node->key_name);
				}
				//DEBUG_MSG("\tClear node [%p], name =[%s]", node->value.v_list.first, node->value.v_list.first->key_name);
				kvFree(node->value.v_list.first);
			}
			node->value.v_list.first = NULL;
			node->value.v_list.last = NULL; 
		break;
		case KV_STRING	: mStringClear(&(node->value.v_string)); break;
		case KV_JSON	: mStringClear(&(node->value.v_json)); break;
		case KV_BOOL	: node->value.v_bool = false; break;
		case KV_INT		: node->value.v_int = 0; break;
		case KV_DOUBLE	: node->value.v_double = 0.0; break;
		case KV_DATETIME:
			node->value.v_datetime.ts = 0;
			node->value.v_datetime.format = NULL;
		break;
		case KV_FUNCTION: node->value.v_function = NULL; break;
		case KV_POINTER	: 
			if(node->value.v_pointer.ptr && node->value.v_pointer.free){
				node->value.v_pointer.free(node->value.v_pointer.ptr);
			}
			node->value.v_pointer.ptr = NULL;
			node->value.v_pointer.free = NULL;
		break;
		case KV_NULL:
		default: break;
	}
	return node;
}//END: kvClear



/*
 * Изъятие KV из структуры KV
 */
kv_s *
kvRemove(kv_s * node){
	if(node==NULL) return NULL;
	if(node->prev) node->prev->next = node->next;
	if(node->next) node->next->prev = node->prev;
	if(node->parent){
		if(node->parent->type == KV_ARRAY || node->parent->type == KV_OBJECT){ 
			if(node->parent->value.v_list.first == node){
				node->parent->value.v_list.first = node->next;
			}
			if(node->parent->value.v_list.last == node){
				node->parent->value.v_list.last = node->prev;
			}
		}
	}
	node->parent = NULL;
	node->prev = NULL;
	node->next = NULL;
	return node;
}//END: kvRemove



/*
 * Освобождение памяти, занятой KV
 */
void
kvFree(kv_s * node){
	if(node==NULL) return;
	kvClear(kvRemove(node));
#ifdef KV_KEY_NAME_IS_DYNAMIC
	mFree(node->key_name);
	node->key_name = NULL;
#else
	node->key_name[0] = '\0';
#endif
	node->key_len = 0;
	node->key_hash = 0;
	_kvToIdle(node);
	return;
}//END: kvFree



/*
 * Изменяет имя ключа
 */
kv_s *
kvSetKey(kv_s * node, const char * key_name, uint32_t key_len){
	if(!key_name) return node;
	if(!key_len) key_len = strlen(key_name);
#ifdef KV_KEY_NAME_IS_DYNAMIC
	if(node->key_name) mFree(node->key_name);
	node->key_name = hashStringCloneCaseN(key_name, key_len, &node->key_len, &node->key_hash);
#else
	hashStringCopyCaseN(node->key_name, key_name, min(key_len, KV_KEY_NAME_LEN), &node->key_len, &node->key_hash);
#endif
	return node;
}//END: kvSetKey



/*
 * Изменяет тип данных в элементе
 */
kv_s *
kvSetType(kv_s * node, kv_t new_type){
	if(node->type == new_type) return node;
	if(node->type == KV_NULL){
		node->type = new_type;
		return node;
	}
	if((node->type == KV_ARRAY || node->type == KV_OBJECT)&&(new_type == KV_ARRAY || new_type == KV_OBJECT)){
		node->type = new_type;
		return node;
	}
	kvClear(node);
	node->type = new_type;
	return node;
}//END: kvSetType



/*
 * Проверяет, пустое ли значение KV или нет (пустая строка, пустой массив, пустой объект, 0 или false)
 */
bool
kvIsEmpty(kv_s * node){
	switch(node->type){
		case KV_NULL: return true;
		case KV_BOOL: return node->value.v_bool;
		case KV_INT: return (node->value.v_int == 0 ? true : false);
		case KV_DOUBLE: return (node->value.v_double == 0 ? true : false);
		case KV_DATETIME: return (node->value.v_datetime.ts == 0 ? true : false);
		case KV_STRING: return (!node->value.v_string.ptr || !node->value.v_string.len ? true : false);
		case KV_JSON: return (!node->value.v_json.ptr || !node->value.v_json.len ? true : false);
		case KV_ARRAY:
		case KV_OBJECT: return (!node->value.v_list.first ? true : false);
		case KV_FUNCTION: return (!node->value.v_function ? true : false);
		case KV_POINTER: return (!node->value.v_pointer.ptr ? true : false);
		default: return true;
	}
}//END: kvIsEmpty



/*
 * Объединяет KV объекты типа KV_OBJECT, перенося все содержимое в объект to из from, объект from уничтожается
 */
kv_s * 
kvMerge(kv_s * to, kv_s * from, kv_rewrite_rule rewrite){
	if(!to) return NULL;
	if(to->type != KV_OBJECT) kvSetType(to, KV_OBJECT);
	if(!from) return to;
	if(from->type != KV_OBJECT){
#ifdef KV_KEY_NAME_IS_DYNAMIC
		if(from->key_name && from->key_len > 0){
#else
		if(from->key_name[0] != '\0' && from->key_len > 0){
#endif
			if(kvInsert(to, from, rewrite)!=KVR_OK) kvFree(from);
		}else{
			kvFree(from);
		}
		return to;
	}
	kv_s * node = from->value.v_list.first;
	while(from->value.v_list.first){
		node = kvRemove(from->value.v_list.first);
		if(kvInsert(to, node, rewrite)!=KVR_OK) kvFree(node);
	}
	kvFree(from);
	return to;
}//END: kvMerge



/*
 * Заменяет запись dst записью src, при этом остается только одна запись dst (src удаляется)
 * Возвращает указатель на новую запись
 */
kv_s *
kvReplace(kv_s * dst, kv_s * src){
	if(!src || !dst) return NULL;
	kvRemove(src);	//Изъятие KV из структуры KV
	kvClear(dst);	//Удаление значения dst
	//Тип значения
	dst->type		= src->type;
	//Копирование ключа
#ifdef KV_KEY_NAME_IS_DYNAMIC
	if(dst->key_name) mFree(dst->key_name);
	dst->key_name	= src->key_name;
#else
	stringCopyN(dst->key_name, src->key_name, src->key_len);
#endif
	dst->key_len	= src->key_len;
	dst->key_hash	= src->key_hash;
	//Копирование значения из src в dst
	memcpy(&dst->value, &src->value, sizeof(kv_value));
	_kvToIdle(src);
	return dst;
}//END: kvReplace



/*
 * Копирует объекты KV из src в dst, возвращает dst
 * В тип KV_POINTER копируется только указатель
 */
kv_s *
kvCopy(kv_s * dst, kv_s * src){
	if(!src) return dst;
	if(!dst) dst = kvNew();
	kv_s * node, * n;
	switch(src->type){

		//NULL Null
		case KV_NULL:
			kvSetNull(dst);
		break;

		//function
		case KV_FUNCTION:
			kvSetFunction(dst, src->value.v_function);
		break;

		//pointer
		case KV_POINTER:
			kvSetPointer(dst, src->value.v_pointer.ptr, src->value.v_pointer.free);
		break;

		//Булево значение True / False
		case KV_BOOL:
			kvSetBool(dst, src->value.v_bool);
		break;

		//Целое число 123
		case KV_INT:
			kvSetInt(dst, src->value.v_int);
		break;

		//Вещественное число 123.456
		case KV_DOUBLE:
			kvSetDouble(dst, src->value.v_double);
		break;

		//Текстовое значение ""
		case KV_STRING:
			kvSetString(dst, src->value.v_string.ptr, src->value.v_string.len);
		break;

		//Текстовое значение в формате JSON
		case KV_JSON:
			kvSetJson(dst, src->value.v_json.ptr, src->value.v_json.len);
		break;

		//Форматированная строка с датой и временем
		case KV_DATETIME:
			kvSetDatetime(dst, src->value.v_datetime.ts, src->value.v_datetime.format);
		break;


		//Массив порядковый []
		case KV_ARRAY:
			kvSetType(dst, KV_ARRAY);
			for(node = src->value.v_list.first; node != NULL; node = node->next){
				kvCopy(kvAppend(dst, NULL, 0, KV_INSERT), node);
			}
		break;

		////Массив ассоциативный {}
		case KV_OBJECT:
			kvSetType(dst, KV_OBJECT);
			for(node = src->value.v_list.first; node != NULL; node = node->next){
				#ifdef KV_KEY_NAME_IS_DYNAMIC
				if(!node->key_name || !node->key_len) continue;
				#else
				if(!node->key_name[0] || !node->key_len) continue;
				#endif
				n = kvAppend(dst, node->key_name, node->key_len, KV_INSERT);
				kvCopy(n, node);
			}
		break;
	}

	return dst;
}//END: kvCopy




/*
 * Заполняет объект dst значениями из src, при совпадении ключей в dst и src
 * Возвращает dst, src не меняется
 */
kv_s *
kvFill(kv_s * dst, kv_s * src){
	if(!src || src->type != KV_OBJECT) return dst;
	if(!dst) return kvCopy(NULL, src);
	if(dst->type != KV_OBJECT) kvSetType(dst, KV_OBJECT);
	kv_s * dst_kv;
	kv_s * src_kv;
	for(dst_kv = dst->value.v_list.first; dst_kv != NULL; dst_kv = dst_kv->next){
		#ifdef KV_KEY_NAME_IS_DYNAMIC
		if(!dst_kv->key_name || dst_kv->key_len == 0) continue;
		#else
		if(dst_kv->key_name[0] == '\0' || dst_kv->key_len == 0) continue;
		#endif
		if((src_kv = kvSearchHash(src, dst_kv->key_name, dst_kv->key_len, dst_kv->key_hash)) == NULL) continue;
		kvCopy(dst_kv, src_kv);
	}//for
	return dst;
}//END: kvFill




/*
 * Создает новый объект KV, который объединяет kv1 и kv2 по совпадающим ключам, при этом в результирующем объекте используются значения из kv1
 * Возвращает новый объект KV, при этом kv1 и kv2 не меняются
 * В данной функции kv1 следует рассматривать как источник значений, а kv2 - как фильтр ключей
 */
kv_s *
kvIntersect(kv_s * kv1, kv_s * kv2){
	if(!kv1 || kv1->type != KV_OBJECT) return NULL;
	if(!kv2 || kv2->type != KV_OBJECT) return NULL;
	kv_s * result = kvNewRoot();
	kv_s * kv1_kv;
	kv_s * kv2_kv;
	for(kv1_kv = kv1->value.v_list.first; kv1_kv != NULL; kv1_kv = kv1_kv->next){
		#ifdef KV_KEY_NAME_IS_DYNAMIC
		if(!kv1_kv->key_name || kv1_kv->key_len == 0) continue;
		#else
		if(kv1_kv->key_name[0] == '\0' || kv1_kv->key_len == 0) continue;
		#endif
		if((kv2_kv = kvSearchHash(kv2, kv1_kv->key_name, kv1_kv->key_len, kv1_kv->key_hash)) == NULL) continue;
		kvCopy(kvAppend(result, kv1_kv->key_name, kv1_kv->key_len, KV_INSERT), kv1_kv);
	}//for
	return result;
}//END: kvIntersect








/***********************************************************************
 * Поиск
 **********************************************************************/


/*
 * Ищет KV с указанным именем в родительской ноде
 */
kv_s *
kvSearch(kv_s * parent, const char * key_name, uint32_t key_len){
	if(!parent || !key_name) return NULL;
#ifndef KV_KEY_NAME_IS_DYNAMIC
	key_len = min(KV_KEY_NAME_LEN, key_len);
#endif
	uint32_t hash = (!key_len ? hashStringCase(key_name, &key_len) : hashStringCaseN(key_name, key_len, &key_len) );
	if(parent->type == KV_OBJECT){
		kv_s * node = parent->value.v_list.first;
		while(node){
#ifdef KV_KEY_NAME_IS_DYNAMIC
			if(node->key_name != NULL && node->key_len > 0){
#else
			if(node->key_name[0] != '\0' && node->key_len > 0){
#endif
				if(
					key_len == node->key_len &&
					hash == node->key_hash &&  
					stringCompareCaseN(node->key_name, key_name, key_len)
				) return node;
			}
			node = node->next;
		}
	}
	return NULL;
}//END: kvSearch



/*
 * Ищет KV с указанным именем в родительской ноде
 */
kv_s *
kvSearchHash(kv_s * parent, const char * key_name, uint32_t key_len, uint32_t hash){
	if(!parent || !key_name) return NULL;
#ifndef KV_KEY_NAME_IS_DYNAMIC
	key_len = min(KV_KEY_NAME_LEN, key_len);
#endif
	if(parent->type == KV_OBJECT){
		kv_s * node = parent->value.v_list.first;
		while(node){
#ifdef KV_KEY_NAME_IS_DYNAMIC
			if(node->key_name != NULL && node->key_len > 0){
#else
			if(node->key_name[0] != '\0' && node->key_len > 0){
#endif
				if(
					hash == node->key_hash && 
					key_len == node->key_len && 
					stringCompareCaseN(node->key_name, key_name, key_len)
				) return node;
			}
			node = node->next;
		}
	}
	return NULL;
}//END: kvSearchHash






/***********************************************************************
 * Вставка KV
 **********************************************************************/

/*
 * Добавляет дочерний KV родителю KV
 */
kv_result
kvInsert(kv_s * parent, kv_s * child, kv_rewrite_rule rewrite){
	if(!parent||!child) return KVR_ERROR;
	kv_s * dst;
#ifdef KV_KEY_NAME_IS_DYNAMIC
	kv_t need_type = (!child->key_name || !child->key_len ? KV_ARRAY : KV_OBJECT);
#else
	kv_t need_type = (!child->key_name[0] || !child->key_len ? KV_ARRAY : KV_OBJECT);
#endif
	if(parent->type != need_type && parent->type != KV_OBJECT) kvSetType(parent, need_type);
	if(parent->type != need_type) return KVR_ERROR;
	if(need_type == KV_OBJECT){
		switch(rewrite){
			//Не вставлять новую запись при нахождении KV с идентичным ключем
			case KV_BREAK:
				if(kvSearchHash(parent, child->key_name, child->key_len, child->key_hash)!=NULL) return KVR_EXISTS;
			break;
			//Заменить существующую запись новой при нахождении KV с идентичным ключем
			case KV_REPLACE:
				if((dst = kvSearchHash(parent, child->key_name, child->key_len, child->key_hash))!=NULL){
					return (!kvReplace(dst, child) ? KVR_ERROR : KVR_OK);
				}
			break;
			//Вставить новую запить
			case KV_INSERT:
			break;
		}
	}
	if(parent->value.v_list.last){
		child->prev = parent->value.v_list.last;
		parent->value.v_list.last->next = child;
	}
	if(!parent->value.v_list.first) parent->value.v_list.first = child;
	parent->value.v_list.last = child;
	child->parent = parent;
	return KVR_OK;
}//END: kvInsert





/*
 * Создает дочерний KV в родительском элементе
 */
kv_s *
kvAppend(kv_s * parent, const char * key_name, uint32_t key_len, kv_rewrite_rule rewrite){
	kv_s * node;
	kv_t need_type = KV_ARRAY;
	uint32_t hash = 0;
#ifndef KV_KEY_NAME_IS_DYNAMIC
	key_len = min(KV_KEY_NAME_LEN, key_len);
#endif
	if(key_name){
#ifdef KV_KEY_NAME_IS_DYNAMIC
		hash = (!key_len ? hashStringCase(key_name, &key_len) : hashStringCaseN(key_name, key_len, &key_len) );
#else
		hash = (!key_len ? hashStringCaseN(key_name, KV_KEY_NAME_LEN, &key_len) : hashStringCaseN(key_name, key_len, &key_len) );
#endif
		switch(rewrite){
			//Не вставлять новую запись при нахождении KV с идентичным ключем
			case KV_BREAK:
				if((node = kvSearchHash(parent, key_name, key_len, hash))!=NULL) return NULL;
			break;
			//Заменить существующую запись новой при нахождении KV с идентичным ключем
			case KV_REPLACE:
				if((node = kvSearchHash(parent, key_name, key_len, hash))!=NULL) return node;
			break;
			//Вставить новую запить
			case KV_INSERT:
			break;
		}
		need_type = KV_OBJECT;
	}
	node = kvNew();
	if(parent){
		if(parent->type != KV_OBJECT && parent->type != KV_ARRAY){
			kvSetType(parent, need_type);
		}
		if(parent->type != need_type && need_type == KV_OBJECT){
			parent->type = KV_OBJECT;
		}
		if(parent->value.v_list.last){
			node->prev = parent->value.v_list.last;
			parent->value.v_list.last->next = node;
		}
		if(!parent->value.v_list.first) parent->value.v_list.first = node;
		parent->value.v_list.last = node;
		node->parent = parent;
	}
	if(key_name){
#ifdef KV_KEY_NAME_IS_DYNAMIC
		node->key_name = stringCloneN(key_name, key_len, &node->key_len);
#else
		node->key_len = stringCopyN(node->key_name, key_name, key_len);
#endif
		node->key_hash = hash;
	}
	return node;
}//END: kvAppend



/*
 * Добавляет в KV значение типа KV_STRING
 * При этом str копируется во внутреннюю переменную
 */
inline kv_s *
kvSetString(kv_s * node, const char * str, uint32_t len){
	if(!node) return NULL;
	kvSetType(kvClear(node), KV_STRING);
	if(str != NULL){
		if(!len)
			node->value.v_string.ptr = stringClone(str, &(node->value.v_string.len));
		else 
			node->value.v_string.ptr = stringCloneN(str, len, &(node->value.v_string.len));
	}
	return node;
}//END: kvSetString



/*
 * Добавляет в KV значение типа KV_STRING
 * При этом внутренней переменной присваивается указатель на str без копирования
 */
inline kv_s *
kvSetStringPtr(kv_s * node, char * str, uint32_t len){
	if(!node) return NULL;
	kvSetType(kvClear(node), KV_STRING);
	if(str != NULL){
		if(!len) len = strlen(str);
		node->value.v_string.ptr = str;
		node->value.v_string.len = len;
	}
	return node;
}//END: kvSetStringPtr



/*
 * Добавляет в KV значение типа KV_JSON
 * При этом str копируется во внутреннюю переменную
 */
inline kv_s *
kvSetJson(kv_s * node, const char * str, uint32_t len){
	if(!node) return NULL;
	kvSetType(kvClear(node), KV_JSON);
	if(str != NULL){
		if(!len)
			node->value.v_json.ptr = stringClone(str, &(node->value.v_json.len));
		else 
			node->value.v_json.ptr = stringCloneN(str, len, &(node->value.v_json.len));
	}
	return node;
}//END: kvSetJson



/*
 * Добавляет в KV значение типа KV_JSON
 * При этом внутренней переменной присваивается указатель на str без копирования
 */
inline kv_s *
kvSetJsonPtr(kv_s * node, char * str, uint32_t len){
	if(!node) return NULL;
	kvSetType(kvClear(node), KV_JSON);
	if(str != NULL){
		if(!len) len = strlen(str);
		node->value.v_json.ptr = str;
		node->value.v_json.len = len;
	}
	return node;
}//END: kvSetJsonPtr



/*
 * Добавляет в KV значение типа KV_BOOL
 */
inline kv_s *
kvSetBool(kv_s * node, bool v_bool){
	if(!node) return NULL;
	kvSetType(kvClear(node), KV_BOOL);
	node->value.v_bool = v_bool;
	return node;
}//END: kvSetBool



/*
 * Добавляет в KV значение типа KV_INT
 */
inline kv_s *
kvSetInt(kv_s * node, int64_t v_int){
	if(!node) return NULL;
	kvSetType(kvClear(node), KV_INT);
	node->value.v_int = v_int;
	return node;
}//END: kvSetInt



/*
 * Добавляет в KV значение типа KV_DOUBLE
 */
inline kv_s *
kvSetDouble(kv_s * node, double v_double){
	if(!node) return NULL;
	kvSetType(kvClear(node), KV_DOUBLE);
	node->value.v_double = v_double;
	return node;
}//END: kvSetDouble



/*
 * Добавляет в KV значение типа KV_NULL
 */
inline kv_s *
kvSetNull(kv_s * node){
	if(!node) return NULL;
	return kvSetType(kvClear(node), KV_NULL);
}//END: kvSetNull



/*
 * Добавляет в KV значение типа KV_FUNCTION
 */
inline kv_s *
kvSetFunction(kv_s * node, void * v_function){
	if(!node) return NULL;
	kvSetType(kvClear(node), KV_FUNCTION);
	node->value.v_function = v_function;
	return node;
}//END: kvSetFunction



/*
 * Добавляет в KV значение типа KV_POINTER
 */
inline kv_s *
kvSetPointer(kv_s * node, void * ptr, v_pointer_free_cb cb){
	if(!node) return NULL;
	kvSetType(kvClear(node), KV_POINTER);
	node->value.v_pointer.ptr = ptr;
	node->value.v_pointer.free = cb;
	return node;
}//END: kvSetPointer




/*
 * Добавляет в KV значение типа KV_DATETIME
 */
inline kv_s *
kvSetDatetime(kv_s * node, time_t ts, const char * format){
	if(!node) return NULL;
	kvSetType(kvClear(node), KV_DATETIME);
	node->value.v_datetime.ts = (!ts ? time(NULL) : ts);
	node->value.v_datetime.format = (!format ? XG_DATETIME_GMT_FORMAT : format);
	return node;
}//END: kvSetDatetime




/*
 * Добавляет дочерний KV типа KV_DATETIME родителю 
 */
inline kv_s *
kvAppendDatetime(kv_s * parent, const char * key_name, time_t ts, const char * format, kv_rewrite_rule rewrite){
	return kvSetDatetime(kvAppend(parent, key_name, 0, rewrite), ts, format);
}//END: kvAppendDatetime




/*
 * Добавляет дочерний KV типа KV_NULL родителю 
 */
inline kv_s *
kvAppendNull(kv_s * parent, const char * key_name, kv_rewrite_rule rewrite){
	return kvSetNull(kvAppend(parent, key_name, 0, rewrite));
}//END: kvAppendNull



/*
 * Добавляет дочерний KV типа KV_BOOL родителю 
 */
inline kv_s *
kvAppendBool(kv_s * parent, const char * key_name, bool value, kv_rewrite_rule rewrite){
	return kvSetBool(kvAppend(parent, key_name, 0, rewrite), value);
}//END: kvAppendBool



/*
 * Добавляет дочерний KV типа KV_INT родителю 
 */
inline kv_s *
kvAppendInt(kv_s * parent, const char * key_name, int64_t value, kv_rewrite_rule rewrite){
	return kvSetInt(kvAppend(parent, key_name, 0, rewrite), value);
}//END: kvAppendInt



/*
 * Добавляет дочерний KV типа KV_DOUBLE родителю 
 */
inline kv_s *
kvAppendDouble(kv_s * parent, const char * key_name, double value, kv_rewrite_rule rewrite){
	return kvSetDouble(kvAppend(parent, key_name, 0, rewrite), value);
}//END: kvAppendDouble



/*
 * Добавляет дочерний KV типа KV_STRING родителю 
 */
inline kv_s *
kvAppendString(kv_s * parent, const char * key_name, const char * value, uint32_t value_len, kv_rewrite_rule rewrite){
	return kvSetString(kvAppend(parent, key_name, 0, rewrite), value, value_len);
}//END: kvAppendString



/*
 * Добавляет дочерний KV типа KV_STRING родителю 
 */
inline kv_s *
kvAppendStringPtr(kv_s * parent, const char * key_name, char * value, uint32_t value_len, kv_rewrite_rule rewrite){
	return kvSetStringPtr(kvAppend(parent, key_name, 0, rewrite), value, value_len);
}//END: kvAppendStringPtr



/*
 * Добавляет дочерний KV типа KV_JSON родителю 
 */
inline kv_s *
kvAppendJson(kv_s * parent, const char * key_name, const char * value, uint32_t value_len, kv_rewrite_rule rewrite){
	return kvSetJson(kvAppend(parent, key_name, 0, rewrite), value, value_len);
}//END: kvAppendJson



/*
 * Добавляет дочерний KV типа KV_ARRAY родителю 
 */
inline kv_s *
kvAppendArray(kv_s * parent, const char * key_name, kv_rewrite_rule rewrite){
	return kvSetType(kvAppend(parent, key_name, 0, rewrite), KV_ARRAY);
}//END: kvAppendArray



/*
 * Добавляет дочерний KV типа KV_OBJECT родителю 
 */
inline kv_s *
kvAppendObject(kv_s * parent, const char * key_name, kv_rewrite_rule rewrite){
	return kvSetType(kvAppend(parent, key_name, 0, rewrite), KV_OBJECT);
}//END: kvAppendObject


/*
 * Добавляет дочерний KV типа KV_FUNCTION родителю 
 */
inline kv_s *
kvAppendFunction(kv_s * parent, const char * key_name, void * value, kv_rewrite_rule rewrite){
	return kvSetFunction(kvAppend(parent, key_name, 0, rewrite), value);
}//END: kvAppendFunction


/*
 * Добавляет дочерний KV типа KV_POINTER родителю 
 */
inline kv_s *
kvAppendPointer(kv_s * parent, const char * key_name, void * ptr, v_pointer_free_cb cb, kv_rewrite_rule rewrite){
	return kvSetPointer(kvAppend(parent, key_name, 0, rewrite), ptr, cb);
}//END: kvAppendPointer







/***********************************************************************
 * Парсер JSON / QUERY -> KV
 **********************************************************************/

//Прототипы функций
static int	_kvJsonIsEndQuote(const char * ptr);
static const char *	_kvJsonSkip(const char * ptr);
static const char *	_kvJsonString(kv_s * current, const char * ptr, kv_jsonp_flag flags);
static const char *	_kvJsonNumber(kv_s * current, const char * ptr, kv_jsonp_flag flags);
static const char *	_kvJsonArray(kv_s * parent, const char * ptr, kv_jsonp_flag flags);
static const char *	_kvJsonObject(kv_s * parent, const char * ptr, kv_jsonp_flag flags);
static const char *	_kvJsonValue(kv_s * current, const char * ptr, kv_jsonp_flag flags);
static const char *	_kvJsonInclude(kv_s * current, const char * ptr, kv_jsonp_flag flags);
static const char *	_kvJsonKey(const char * ptr, const char ** key, uint32_t * key_n, kv_jsonp_flag flags);


//Проверяет, является ли символ ["] закрывающим, 1 - закрывающий, 0 - в контексте (не закрывающий)
static int
_kvJsonIsEndQuote(const char * ptr){
	if(*ptr !='"') return 0;
	int endq = 1; ptr--;
	while(*ptr == '\\'){endq = 1-endq; ptr--;}
	return endq;
}



//Пропускает символы меньше (int)32, включая перенос строк
static const char *
_kvJsonSkip(const char * ptr){
	while(*ptr){
		//Пропускаем символы меньше 0x21 (int)33
		if(*ptr < 0x21){ptr++;continue;}
		//Пропускаем строчный комментарий # или //
		if(*ptr == '#' || *(uint16_t *)ptr == 0x2F2F){
			while(*ptr && *ptr!='\n') ptr++;
			if(*ptr) ptr++;
			continue;
		}
		//Пропускаем блочный комментарий /* ... */
		if(*(uint16_t *)ptr == 0x2A2F){ptr+=2;
			while(*ptr && *(uint16_t *)ptr != 0x2F2A) ptr++;
			if(*ptr) ptr+=2;
			continue;
		}
		return ptr;
	}
	return ptr;
}



//Парсинг подключаемого файла
static const char *
_kvJsonInclude(kv_s * current, const char * ptr, kv_jsonp_flag flags){
	if(!stringCompareCaseN(ptr,"include",7)) RETURN_ERROR(NULL, "ptr != 'include'");
	ptr = _kvJsonSkip(ptr+7);
	if(*ptr != '(') RETURN_ERROR(NULL, "*ptr != '(' =[%c]", *ptr); ptr++;
	if(*ptr == '"') ptr++;
	ptr = _kvJsonSkip(ptr);
	const char * tmp = ptr;
	while(*ptr && *ptr!='"' && *ptr != ')' && !isspace((int)*ptr)) ptr++;
	if(*ptr && flags & KVJF_ALLOW_INCLUDE){
		char * filename = stringCloneN(tmp, ptr-tmp, NULL);
		kv_s * node = kvFromJsonFile(filename, flags);
		mFree(filename);
		if(node){
			kvMerge(current, node, true);
		}else{
			kvSetType(current, KV_NULL);
		}
	}
	while(*ptr && *ptr!=')')ptr++;

	//Найден конец include
	if (*ptr==')') return ptr+1;

	//Иначе, ошибка
	RETURN_ERROR(NULL, "*ptr != ')' =[%c]", *ptr);
}


//Парсинг ключа
static const char *
_kvJsonKey(const char * ptr, const char ** key, uint32_t * key_n, kv_jsonp_flag flags){
	*key_n = 0;
	*key = NULL;
	bool quoted = false;
	ptr = _kvJsonSkip(ptr);
	if(*ptr == '"'){
		quoted = true;
		ptr++;
	}else{
		quoted = false;
	}
	*key = ptr;

	//Если ключ задан без кавычек
	if(!quoted){
		while(*ptr && charIsUnreserved(*ptr)) ptr++;
	}else{
		while(*ptr && (*ptr!='"' || (*ptr=='"' && !_kvJsonIsEndQuote(ptr)))) ptr++;
	}

	*key_n = ptr - *key;

	if(quoted) ptr++;
	return _kvJsonSkip(ptr);
}



//Парсинг строки
static const char *
_kvJsonString(kv_s * current, const char * ptr, kv_jsonp_flag flags){
	if(*ptr != '"') RETURN_ERROR(NULL, "*ptr != '\"' =[%c]", *ptr); ptr++;
	const char * value = ptr;
	while(*ptr && (*ptr!='"' || (*ptr=='"' && !_kvJsonIsEndQuote(ptr)))) ptr++;
	if(!*ptr) RETURN_ERROR(NULL, "*ptr == 0");
	uint32_t n = ptr - value;
	if(n>0){
		buffer_s * buf = decodeJson(value, n, NULL);
		if(buf){
			kvSetString(current, buf->buffer, buf->count);
			bufferFree(buf);
		}else{
			kvSetStringPtr(current, NULL, 0);
		}
	}else{
		kvSetStringPtr(current, NULL, 0);
	}
	return ptr + 1;
}



//Парсинг числа
static const char *
_kvJsonNumber(kv_s * current, const char * ptr, kv_jsonp_flag flags){

	int64_t v_int = 0;
	double v_double = 0;
	bool is_int = true;
	double sign=1, scale=0;
	int subscale=0, signsubscale=1;

	//Отрицательное число
	if (*ptr=='-'){sign=-1;ptr++;}
	while(*ptr == '0') ptr++;	//Пропускаем нули в начале значения
	//целая часть числа
	if(*ptr>='1' && *ptr<='9'){
		do{
			v_int = (v_int*10)+(*ptr++ -'0');
		}while(*ptr>='0' && *ptr<='9');
	}

	v_double = (double)v_int;

	//Дробная часть числа
	if(*ptr=='.' && ptr[1]>='0' && ptr[1]<='9'){
		is_int = false; ptr++;
		do{
			v_double=(v_double*10.0)+(*ptr++ -'0');
			scale--;
		}while (*ptr>='0' && *ptr<='9');
	}
	//Экспонента
	if (*ptr=='e' || *ptr=='E'){
		is_int = false; ptr++;
		if(*ptr=='+') ptr++;
		else 
		if(*ptr=='-'){signsubscale=-1; ptr++;}
		while (*ptr>='0' && *ptr<='9') subscale=(subscale*10)+(*ptr++ - '0');
	}

	if (*ptr=='f' || *ptr=='F'){is_int = false; ptr++;}	//Число явно задано как Double
	if (*ptr=='i' || *ptr=='I' || *ptr=='L' || *ptr=='l'){is_int = true; ptr++;}	//Число явно задано как INT

	if(is_int){
		v_int = sign * v_int;
		kvSetInt(current, v_int);
	}else{
		v_double = sign * v_double * pow(10.0, (scale + subscale * signsubscale));
		kvSetDouble(current, v_double);
	}

	return ptr;
}



//Парсинг массива
static const char *
_kvJsonArray(kv_s * parent, const char * ptr, kv_jsonp_flag flags){

	kv_s * child;
	if (*ptr != '[') RETURN_ERROR(NULL, "*ptr != '[' =[%c]", *ptr);
	kvSetType(parent, KV_ARRAY);
	ptr = _kvJsonSkip(ptr+1);
	if(*ptr==']') return ptr + 1;	//Пустой массив

	do{
		if(*ptr ==',')ptr++;
		ptr = _kvJsonSkip(ptr);
		if(*ptr == ']') break;
		child = kvAppend(parent, NULL, 0, KV_INSERT);
		ptr = _kvJsonValue(child, _kvJsonSkip(ptr), flags); //Пропускаем пробелы, получаем значение
		if (!ptr) RETURN_ERROR(NULL, "!ptr");
		ptr = _kvJsonSkip(ptr);
	}while(*ptr == ',');

	//Найден конец массива
	if (*ptr==']') return ptr+1;

	//Иначе, ошибка
	RETURN_ERROR(NULL, "*ptr != ']' =[%c]", *ptr);
}



//Парсинг объекта
static const char *
_kvJsonObject(kv_s * parent, const char * ptr, kv_jsonp_flag flags){

	kv_s * child;
	const char * key;
	uint32_t key_n;
	if (*ptr != '{') RETURN_ERROR(NULL, "*ptr != '{' -> [%c]", *ptr);
	kvSetType(parent, KV_OBJECT);
	ptr = _kvJsonSkip(ptr+1);
	if(*ptr=='}') return ptr + 1;	//Пустой объект

	do{
		if(*ptr ==',')ptr++;
		ptr = _kvJsonSkip(ptr);
		if(*ptr == '}') break;
		ptr = _kvJsonKey(ptr, &key, &key_n, flags);
		if(!key || !key_n) RETURN_ERROR(NULL, "!key || !key_n [%u]",key_n);
		if(*ptr !=':') RETURN_ERROR(NULL, "*ptr != ':' -> [%c]", *ptr);
		child = kvAppend(parent, key, key_n, KV_REPLACE);
		ptr = _kvJsonValue(child, _kvJsonSkip(ptr+1), flags); //Пропускаем пробелы, получаем значение
		if (!ptr) RETURN_ERROR(NULL, "!ptr");
		ptr = _kvJsonSkip(ptr);
	}while(*ptr == ',');

	//Найден конец объекта
	if (*ptr=='}') return ptr+1;

	//Иначе, ошибка
	RETURN_ERROR(NULL, "*ptr != '}' =[%c] -> nearly ...[[%s]]...", *ptr, stringCloneN(ptr,60,NULL));
}



//Парсинг значения
static const char *
_kvJsonValue(kv_s * current, const char * ptr, kv_jsonp_flag flags){

	if(!ptr) RETURN_ERROR(NULL, "!ptr");

	//бесконечный цикл
	while(*ptr){

		//Пропускаем пробелы и комментарии
		ptr = _kvJsonSkip(ptr);

		switch(*ptr){

			//null
			case 'n': case 'N':
				if(stringCompareCaseN(ptr,"null",4)){current->type=KV_NULL; return ptr+4;}
			break;

			//false
			case 'f': case 'F':
				if(stringCompareCaseN(ptr,"false",5)){current->type=KV_BOOL; current->value.v_bool = false; return ptr + 5;}
			break;

			//true
			case 't': case 'T':
				if(stringCompareCaseN(ptr,"true",4)){current->type=KV_BOOL; current->value.v_bool = true; return ptr + 4;}
			break;

			//Строка
			case '"':
				return _kvJsonString(current, ptr, flags);
			break;

			//Число 123, -123, .123
			case '-': case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9': case '.':
				return _kvJsonNumber(current, ptr, flags);
			break;

			//Массив
			case '[':
				return _kvJsonArray(current, ptr, flags);
			break;

			//Объект
			case '{':
				return _kvJsonObject(current, ptr, flags);
			break;

			//Специальные функции
			case '@':
				return _kvJsonInclude(current, ptr+1, flags);
			break;

			//Все остальное - ошибка
			default: RETURN_ERROR(NULL, "*ptr != defined values -> [%c]", *ptr);
		}//switch(*ptr)

		ptr++;
	}//бесконечный цикл

	return ptr;
}



/*
 * Создание дерева KV из JSON текста
 */
kv_s *
kvFromJsonString(const char * json, kv_jsonp_flag flags){

	kv_s * root = kvNewRoot();	//Root KV
	
	if(!_kvJsonValue(root, json, flags)){
		kvFree(root);
		RETURN_ERROR(NULL, "Error while JSON parsing");
	}

	return root;
}//END: kvFromJsonString



/*
 * Создание дерева KV из JSON файла
 */
kv_s *
kvFromJsonFile(const char * filename, kv_jsonp_flag flags){
	int64_t buf_n;
	char * buf = fileRead(filename, 0, 0, &buf_n);
	if(buf_n < 1 || !buf){
		if(buf) mFree(buf);
		RETURN_ERROR(kvNewRoot(), "Read file [%s] fail or empty file", filename);
	}

	//DEBUG_MSG("\nJSON FILE [%s] CONTENT----------\n%s\n---------------------------------------------------------\n",filename, buf);

	kv_s * root;
	bool stop = stringCompareCaseN(buf,"$STOP",5);
	if(flags & KVJF_ALLOW_STOP && stop){
		root = kvNewRoot();
		DEBUG_MSG("\nJSON FILE [%s] STOPPED BY $STOP COMMAND",filename);
	}else{
		root = kvFromJsonString((stop ? buf+5 : buf), flags);
		if(!root){
			mFree(buf);
			RETURN_ERROR(NULL, "Error while parsing file [%s]", filename);
		}
	}
	mFree(buf);

	return root;
}//END: kvFromJsonFile




/*
 * Создание дерева KV из query строки запроса GET или POST (application/x-www-form-urlencoded)
 * Обработка строки запроса:
 * key=value - 
 * key[]=value - массив
 * key[456]=value - массив
 * key[test]=value - объект
 * key[test][]=value - массив test
 * key[test][info]=value - объект test
 * key[][]=value - будет читаться как key[]=value
 * key[][abc]=value - будет читаться как key[]=value
 */
kv_s *
kvFromQueryString(const char * query){

	kv_s * node;
	kv_s * parent;
	kv_s * root = kvNewRoot();
	const char * ptr = query;
	const char * tmp;
	uint32_t len;
	const char * k;
	buffer_s * buf;

	//DEBUG_MSG("\nquery: %s\n",query);

	while(*ptr){

		parent	= root;

		//Ошибка, структура ключ=значение не может начинаться с символа '='
		if(charExists(*ptr,"=[]&")) break;

		//Поиск разделителя "=" (ключ = значение)
		//tmp = strchr(ptr, '=');
		tmp = ptr;
		while((isalnum((int)*tmp) || charExists(*tmp,"[]_-")) && *tmp!='=' && *tmp!='\0') tmp++;
		if(*tmp !=  '=') break;

		//Обработка ключа
		len = tmp - ptr;

		k = ptr; 
		while(k<tmp && *k != '[') k++;
		//Символ '[' не найден, вся строка является именем ключа (key=value)
		if(k == tmp){
			node = kvAppend(parent, ptr, tmp - ptr, KV_REPLACE);
		}
		//Ключ представляет собой массив  (key[...]...=value)
		else{
			node = kvAppend(parent, ptr, k - ptr, true);

			label_repeat_node:

			//Ключ представляет собой массив key[]=value
			if(*k=='['){
				if(*(k+1)==']'|| (k+1) == tmp){
					node = kvAppend(node, NULL, 0, KV_INSERT);
				}else{
					//Пропускаем "["
					k++; ptr = k;
					while(k<tmp && *k != ']') k++;
					node = kvAppend(node, ptr, k - ptr, KV_REPLACE);
					if(*k == ']' && *(k+1)=='['){
						k++;ptr = k;
						goto label_repeat_node;
					}
				}
			}

		}//Ключ представляет собой массив


		//Пропускаем '='
		tmp++;
		ptr = tmp;

		//Обработка значения
		while (*tmp != '\0' && *tmp !='&') tmp++;
		len = tmp - ptr;
		if(len > 0){
			buf = decodeUrlQuery(ptr, len, NULL);
			if(buf){
				kvSetString(node, buf->buffer, buf->count);
				bufferFree(buf);
			}else{
				kvSetStringPtr(node, NULL, 0);
			}
		}

		//Найден конец строки
		if(*tmp == '\0') break;

		//Пропускаем '&'
		tmp++;
		ptr = tmp;

	}//while(1)

	return root;
}//END: kvFromQueryString







/***********************************************************************
 * Вывод стуктуры KV в виде текста
 **********************************************************************/

/*
 * Преобразует значение KV в строку
 */
buffer_s *
kvAsString(kv_s * kv, buffer_s * buf){
	if(!kv) return NULL;
	if(!buf) buf = bufferCreate(0);
	switch(kv->type){
		case KV_NULL:		bufferAddStringN(buf,CONST_STR_COMMA_LEN("null")); break;
		case KV_BOOL:		if(kv->value.v_bool) bufferAddStringN(buf, CONST_STR_COMMA_LEN("true")); else bufferAddStringN(buf, CONST_STR_COMMA_LEN("false")); break;
		case KV_INT:		bufferAddInt(buf, kv->value.v_int); break;
		case KV_DOUBLE:		bufferAddDouble(buf, kv->value.v_double); break;
		case KV_DATETIME:	bufferAddDatetime(buf, kv->value.v_datetime.ts, kv->value.v_datetime.format); break;
		case KV_STRING:
		case KV_JSON: 
							bufferAddStringN(buf, kv->value.v_string.ptr, kv->value.v_string.len);
		break;
		case KV_OBJECT:
		case KV_ARRAY:
							kvEcho(kv, KVF_JSON, buf);
		break;
		default:
		break;
	}
	return buf;
}//END: kvAsString




/*
 * Вывод дерева KV в строку
 */
buffer_s *
kvEcho(kv_s * root, kv_format_t format, buffer_s * buf){

	if(!root) return NULL;

	if(!buf) buf = bufferCreate(0);

	switch(format){
		case KVF_JSON: kvEchoJson(buf, root); break;
		case KVF_URLQUERY: 
			kvEchoQuery(buf, root, 0); 
			if(buf->count > 0 && buf->buffer[buf->count-1] == '&'){
				buf->count--;
				buf->buffer[buf->count] = '\0';
				buf->index = buf->count;
			}
		break;
		case KVF_HEADERS:
			if(root->type != KV_OBJECT) break;
			kvEchoHeaders(buf, root);
		break;
	}

	return buf;
}//END: kvEcho



/*
 * Вывод значения KV в буффер buffer_s в формате JSON
 */
void
kvEchoJson(buffer_s * buf, kv_s * current){

	kv_s * node;
	string_s * s;
	int index;

	switch(current->type){

		//NULL Null
		case KV_NULL:
			bufferAddStringN(buf, "null", 4);
		break;

		//function
		case KV_FUNCTION:
			bufferAddStringN(buf, "\"(function)\"", 12);
		break;

		//pointer
		case KV_POINTER:
			bufferAddStringN(buf, "\"(pointer)\"", 11);
		break;

		//Булево значение True / False
		case KV_BOOL:
			if(current->value.v_bool) bufferAddStringN(buf, "true", 4);
			else bufferAddStringN(buf, "false", 5);
		break;

		//Целое число 123
		case KV_INT:
			bufferAddInt(buf, current->value.v_int);
		break;

		//Вещественное число 123.456
		case KV_DOUBLE:
			bufferAddDouble(buf, current->value.v_double);
		break;

		//Текстовое значение ""
		case KV_STRING:
			bufferAddChar(buf,'\"');
			if(current->value.v_string.ptr && current->value.v_string.len > 0){
				encodeJson(current->value.v_string.ptr, current->value.v_string.len, buf);
			}
			bufferAddChar(buf,'\"');
		break;

		//Текстовое значение в формате JSON
		case KV_JSON:
			bufferAddStringN(buf, current->value.v_json.ptr, current->value.v_json.len);
		break;

		//Форматированная строка с датой и временем
		case KV_DATETIME:
			bufferAddChar(buf,'\"');
			if(current->value.v_datetime.ts > 0 && current->value.v_datetime.format != NULL){
				s = datetimeFormat(current->value.v_datetime.ts, current->value.v_datetime.format);
				encodeJson(s->ptr, s->len, buf);
				mStringFree(s);
			}
			bufferAddChar(buf,'\"');
		break;


		//Массив порядковый []
		case KV_ARRAY:
			bufferAddChar(buf,'[');
			node = current->value.v_list.first;
			if(node){
				for(;;){
					kvEchoJson(buf, node);
					node = node->next;
					if(!node) break;
					bufferAddChar(buf,',');
				}
			}
			bufferAddChar(buf,']');
		break;

		////Массив ассоциативный {}
		case KV_OBJECT:
			bufferAddChar(buf,'{');
			node = current->value.v_list.first;
			if(node){
				index = 0;
				while(node){
#ifdef KV_KEY_NAME_IS_DYNAMIC
					if(!node->key_name || !node->key_len){node = node->next; continue;}
#else
					if(!node->key_name[0] || !node->key_len){node = node->next; continue;}
#endif
					if(index > 0) bufferAddChar(buf,',');
					bufferAddChar(buf,'\"');
					bufferAddStringN(buf, node->key_name, node->key_len);
					bufferAddStringN(buf,"\":",2);
					kvEchoJson(buf, node);
					node = node->next;
					if(!node) break;
					index++;
				}
			}
			bufferAddChar(buf,'}');
		break;
	}

}//END: kvEchoJson



/*
 * Вывод значения KV в буффер buffer_s в формате URL Query
 */
void
kvEchoQuery(buffer_s * buf, kv_s * current, uint32_t depth){

	kv_s * node;
	kv_s * parent;
	string_s path[16];
	string_s * s;
	int path_count = 0, i;

	switch(current->type){

		//NULL Null
		case KV_NULL:
			bufferAddStringN(buf, "=null&", 6);
		break;

		//function
		case KV_FUNCTION:
			bufferAddStringN(buf, "=function&", 10);
		break;

		//pointer
		case KV_POINTER:
			bufferAddStringN(buf, "=pointer&", 9);
		break;

		//Булево значение True / False
		case KV_BOOL:
			if(current->value.v_bool) bufferAddStringN(buf, "=true&", 6);
			else bufferAddStringN(buf, "=false&", 7);
		break;

		//Целое число 123
		case KV_INT:
			bufferAddChar(buf,'=');
			bufferAddInt(buf, current->value.v_int);
			bufferAddChar(buf,'&');
		break;

		//Вещественное число 123.456
		case KV_DOUBLE:
			bufferAddChar(buf,'=');
			bufferAddDouble(buf, current->value.v_double);
			bufferAddChar(buf,'&');
		break;

		//Текстовое значение ""
		case KV_STRING:
			bufferAddChar(buf,'=');
			if(current->value.v_string.ptr && current->value.v_string.len > 0){
				encodeUrlQuery(current->value.v_string.ptr, current->value.v_string.len, buf);
			}
			bufferAddChar(buf,'&');
		break;


		//Текстовое значение в формате JSON
		case KV_JSON:
			bufferAddChar(buf,'=');
			if(current->value.v_json.ptr && current->value.v_json.len > 0){
				encodeUrlQuery(current->value.v_json.ptr, current->value.v_json.len, buf);
			}
			bufferAddChar(buf,'&');
		break;


		//Форматированная строка с датой и временем
		case KV_DATETIME:
			bufferAddChar(buf,'=');
			if(current->value.v_datetime.ts > 0 && current->value.v_datetime.format != NULL){
				s = datetimeFormat(current->value.v_datetime.ts, current->value.v_datetime.format);
				encodeUrlQuery(s->ptr, s->len, buf);
				mStringFree(s);
			}
			bufferAddChar(buf,'&');
		break;

		//Массив порядковый []
		case KV_ARRAY:
#ifdef KV_KEY_NAME_IS_DYNAMIC
			if(depth == 1 && (!current->key_name || !current->key_len)) break;
#else
			if(depth == 1 && (!current->key_name[0] || !current->key_len)) break;
#endif
			node = current->value.v_list.first;
			if(node){
				for(;;){

					//Если дочерний элемент - не массив и не объект -> скалярное значение
					//Добавляем в буффер путь к элементу
					if(node->type != KV_ARRAY && node->type != KV_OBJECT){

						//Вычисление пути
						path_count = 0;
						parent = node;
						while(parent->parent){
							path[path_count].ptr = parent->key_name;
							path[path_count].len = parent->key_len;
							path_count++;
							if(path_count == 8) break;
							parent = parent->parent;
						}

						//Запись пути в буффер
						for(i=path_count-1;i>=0;--i){
							if(path[i].ptr != NULL && i>0){
								if(i == path_count-1){
									bufferAddStringN(buf, path[i].ptr, path[i].len);
								}else{
									bufferAddChar(buf,'[');
									bufferAddStringN(buf, path[i].ptr, path[i].len);
									bufferAddChar(buf,']');
								}
							}else{
								bufferAddStringN(buf,"[]",2);
							}
						}

					}//Если дочерний элемент - не массив и не объект -> скалярное значение

					kvEchoQuery(buf, node, depth + 1);
					node = node->next;
					if(!node) break;
				}
			}
		break;


		////Массив ассоциативный {}
		case KV_OBJECT:
#ifdef KV_KEY_NAME_IS_DYNAMIC
			if(depth > 0 && (!current->key_name || !current->key_len)) break;
#else
			if(depth > 0 && (!current->key_name[0] || !current->key_len)) break;
#endif
			node = current->value.v_list.first;
			if(node){
				while(node){
#ifdef KV_KEY_NAME_IS_DYNAMIC
					if(!node->key_name || !node->key_len){node = node->next; continue;}
#else
					if(!node->key_name[0] || !node->key_len){node = node->next; continue;}
#endif

					//Если дочерний элемент - не массив и не объект -> скалярное значение
					//Добавляем в буффер путь к элементу
					if(node->type != KV_ARRAY && node->type != KV_OBJECT){

						//Вычисление пути
						path_count = 0;
						parent = node;
						while(parent->parent){
							path[path_count].ptr = parent->key_name;
							path[path_count].len = parent->key_len;
							path_count++;
							if(path_count == 8) break;
							parent = parent->parent;
						}

						//Запись пути в буффер
						for(i=path_count-1;i>=0;--i){
							if(i == path_count-1){
								bufferAddStringN(buf, path[i].ptr, path[i].len);
							}else{
								bufferAddChar(buf,'[');
								bufferAddStringN(buf, path[i].ptr, path[i].len);
								bufferAddChar(buf,']');
							}
						}

					}//Если дочерний элемент - не массив и не объект -> скалярное значение

					kvEchoQuery(buf, node, depth + 1);
					node = node->next;
					if(!node) break;
				}//while(node)
			}//if(node)
		break;
	}

}//END: kvEchoQuery



/*
 * Вывод значения KV в буффер buffer_s в формате HTTP Headers
 */
void
kvEchoHeaders(buffer_s * buf, kv_s * parent){

	kv_s * current;

	if(parent->type != KV_OBJECT) return;
	current = parent->value.v_list.first;

	while(current){

#ifdef KV_KEY_NAME_IS_DYNAMIC
		if(!current->key_name || !current->key_len){
#else
		if(!current->key_name[0] || !current->key_len){
#endif
			current = current->next;
			continue;
		}

		switch(current->type){

			//Целое число 123
			case KV_INT:
				bufferAddStringN(buf, current->key_name, current->key_len);
				bufferAddStringN(buf, ": ", 2);
				bufferAddInt(buf, current->value.v_int);
				bufferAddStringN(buf, "\r\n", 2);
			break;

			//Вещественное число 123.456
			case KV_DOUBLE:
				bufferAddStringN(buf, current->key_name, current->key_len);
				bufferAddStringN(buf, ": ", 2);
				bufferAddDouble(buf, current->value.v_double);
				bufferAddStringN(buf, "\r\n", 2);
			break;

			//Текстовое значение ""
			case KV_STRING:
				if(current->value.v_string.ptr && current->value.v_string.len > 0){
					bufferAddStringN(buf, current->key_name, current->key_len);
					bufferAddStringN(buf, ": ", 2);
					bufferAddStringN(buf, current->value.v_string.ptr, current->value.v_string.len);
					bufferAddStringN(buf, "\r\n", 2);
				}
			break;

			//Текстовое значение в формате JSON
			case KV_JSON:
				if(current->value.v_json.ptr && current->value.v_json.len > 0){
					bufferAddStringN(buf, current->key_name, current->key_len);
					bufferAddStringN(buf, ": ", 2);
					bufferAddStringN(buf, current->value.v_json.ptr, current->value.v_json.len);
					bufferAddStringN(buf, "\r\n", 2);
				}
			break;

			//Дата и время в заданном формате
			case KV_DATETIME:
				if(current->value.v_datetime.ts > 0 && current->value.v_datetime.format != NULL){
					bufferAddStringN(buf, current->key_name, current->key_len);
					bufferAddStringN(buf, ": ", 2);
					//printf("kvEchoHeaders datetime(%u, %s)\n",(uint32_t)current->value.v_datetime.ts, current->value.v_datetime.format);
					bufferAddDatetime(buf, current->value.v_datetime.ts, current->value.v_datetime.format);
					bufferAddStringN(buf, "\r\n", 2);
				}
			break;

			default:break;
		}

		current = current->next;
	}
}//END: kvEchoHeaders



/*
 * Возвращает тип KV в виде текста
 */
const char *
kvEchoType(kv_t type){
	switch(type){
		case KV_NULL: return "NULL";
		case KV_BOOL: return "BOOL";
		case KV_INT: return "INT";
		case KV_DOUBLE: return "DOUBLE";
		case KV_STRING: return "STRING";
		case KV_JSON: return "JSON STRING";
		case KV_ARRAY: return "ARRAY";
		case KV_OBJECT: return "OBJECT";
		case KV_FUNCTION: return "FUNCTION";
		case KV_POINTER: return "POINTER";
		case KV_DATETIME: return "DATETIME";
		default:
			return "UNDEFINED";
	}
}//END: kvEchoType




/***********************************************************************
 * Обращение к дочерним элементам KV
 **********************************************************************/


/*
 * Возвращает дочерний KV исходя из указанного имени ключа или NULL, обновленная версия kvSearch
 */
kv_s *
kvGetChild(kv_s * parent, const char * path){
	if(!parent || parent->type != KV_OBJECT) return NULL;
	if(!path) return NULL;
	kv_s * node = parent->value.v_list.first;
	if(!node) return NULL;
	uint32_t hash, n;
	while(*path == '/')path++;
	const char * ptr = path;
	const char * key = path;
	if(!*ptr) return parent;

#ifdef KV_KEY_NAME_IS_DYNAMIC
	while(*ptr && *ptr!='/' && *ptr!='['){
#else
	while(*ptr && *ptr!='/' && *ptr!='[' && n < KV_KEY_NAME_LEN){
#endif
		hash += (u_char)tolower((u_char)*ptr);
		hash -= (hash << 13) | (hash >> 19);
		ptr++; n++;
	}
	if(!n) return NULL;

	//Просмотр дочерних нод и поиск ноды по ключу
	for(;node != NULL; node = node->next){
#ifdef KV_KEY_NAME_IS_DYNAMIC
		if(node->key_name != NULL && node->key_len > 0){
#else
		if(node->key_name[0] != '\0' && node->key_len > 0){
#endif
			if(
				hash == node->key_hash && 
				n == node->key_len && 
				stringCompareCaseN(node->key_name, key, n)
			) return node;
		}
	}//for
	return NULL;
}//END: kvGetChild


bool 
kvGetAsBool(kv_s * parent, const char * path, bool def){
	kv_s * v = (!path ? parent : kvGetChild(parent, path));
	return (!v || v->type != KV_BOOL ? def : v->value.v_bool);
}

int64_t
kvGetAsInt64(kv_s * parent, const char * path, int64_t def){
	kv_s * v = (!path ? parent : kvGetChild(parent, path));
	return (!v || v->type != KV_INT ? def : (int64_t)v->value.v_int);
}

uint64_t
kvGetAsUInt64(kv_s * parent, const char * path, uint64_t def){
	kv_s * v = (!path ? parent : kvGetChild(parent, path));
	return (!v || v->type != KV_INT ? def : (uint64_t)v->value.v_int);
}

int32_t
kvGetAsInt32(kv_s * parent, const char * path, int32_t def){
	kv_s * v = (!path ? parent : kvGetChild(parent, path));
	return (!v || v->type != KV_INT ? def : (int32_t)v->value.v_int);
}

uint32_t
kvGetAsUInt32(kv_s * parent, const char * path, uint32_t def){
	kv_s * v = (!path ? parent : kvGetChild(parent, path));
	return (!v || v->type != KV_INT ? def : (uint32_t)v->value.v_int);
}

double
kvGetAsDouble(kv_s * parent, const char * path, double def){
	kv_s * v = (!path ? parent : kvGetChild(parent, path));
	return (!v || v->type != KV_DOUBLE ? def : (double)v->value.v_double);
}

const char *
kvGetAsString(kv_s * parent, const char * path, const char * def){
	kv_s * v = (!path ? parent : kvGetChild(parent, path));
	return (!v || v->type != KV_STRING ? def : (const char *)v->value.v_string.ptr);
}

const_string_s *
kvGetAsStringS(kv_s * parent, const char * path, const_string_s * def){
	kv_s * v = (!path ? parent : kvGetChild(parent, path));
	return (!v || v->type != KV_STRING ? def : (const_string_s *)&(v->value.v_string));
}

const char *
kvGetAsJson(kv_s * parent, const char * path, const char * def){
	kv_s * v = (!path ? parent : kvGetChild(parent, path));
	return (!v || v->type != KV_JSON ? def : (const char *)v->value.v_json.ptr);
}

const_string_s *
kvGetAsJsonS(kv_s * parent, const char * path, const_string_s * def){
	kv_s * v = (!path ? parent : kvGetChild(parent, path));
	return (!v || v->type != KV_JSON ? def : (const_string_s *)&(v->value.v_json));
}

void *
kvGetAsFunction(kv_s * parent, const char * path, void * def){
	kv_s * v = (!path ? parent : kvGetChild(parent, path));
	return (!v || v->type != KV_FUNCTION ? def : (void *)v->value.v_function);
}

void *
kvGetAsPointer(kv_s * parent, const char * path, void * def){
	kv_s * v = (!path ? parent : kvGetChild(parent, path));
	return (!v || v->type != KV_POINTER ? def : (void *)v->value.v_pointer.ptr);
}









/***********************************************************************
 * Обращение к элементам KV в дереве KV
 **********************************************************************/


/*
 * Возвращает KV по индексу элемента
 */
kv_s *
kvGetByIndex(kv_s * parent, uint32_t index){
	if(!parent) return NULL;
	if(parent->type != KV_ARRAY && parent->type != KV_OBJECT) return NULL;
	uint32_t i = 0;
	kv_s * node = parent->value.v_list.first;
	while(node){
		if(i == index) return node;
		node = node->next;
		i++;
	}
	return NULL;
}//END: kvGetByIndex



/*
 * Возвращает KV исходя из указанного пути или NULL
 * Путь выглядит как для файловой системы, пример: 
 * object/array[2]/var_of_object
 * settings/general/language/default
 */
kv_s *
kvGetByPath(kv_s * root, const char * path){
	if(!root) return NULL;
	if(!path) return root;
	kv_s * node = root;
	uint32_t hash, n;
	while(*path == '/')path++;
	const char * ptr = path;
	const char * key = path;
	if(!*ptr) return root;
	while(*ptr){
		while(*ptr && *ptr!='/' && *ptr!='[') ptr++;
		n = ptr - key;
		hash = hashStringCaseN(key, n, &n);
		node = kvSearchHash(node, key, n, hash);
		label_node_iterator:
		if(!node) return NULL;
		//Если последний символ "/" то считаем что запрошен индекс директории index
		if(*ptr=='/' && !*(ptr+1)){
			return kvSearch(node, "index", 5);
		}
		if(!*ptr || !*(ptr+1)) return node;
		if(*ptr!='[' && node->type != KV_OBJECT) return NULL;
		if(*ptr=='['){
			if(node->type != KV_ARRAY) return NULL;
			key=ptr+1;
			ptr = strchr(key, ']');
			if(!ptr) return NULL;
			node = kvGetByIndex(node, (ptr == key ? 0 : atol(key)));
			ptr++; goto label_node_iterator;
		}
		ptr++;
		key = ptr;
	}
	return NULL;
}//END: kvGetByPath



bool 
kvGetBoolByPath(kv_s * root, const char * path, bool def){
	kv_s * v = kvGetByPath(root, path);
	return (!v || v->type != KV_BOOL ? def : v->value.v_bool);
}

int64_t
kvGetIntByPath(kv_s * root, const char * path, int64_t def){
	kv_s * v = kvGetByPath(root, path);
	return (!v || v->type != KV_INT ? def : v->value.v_int);
}

double
kvGetDoubleByPath(kv_s * root, const char * path, double def){
	kv_s * v = kvGetByPath(root, path);
	return (!v || v->type != KV_DOUBLE ? def : v->value.v_double);
}

const char *
kvGetStringByPath(kv_s * root, const char * path, const char * def){
	kv_s * v = kvGetByPath(root, path);
	return (!v || v->type != KV_STRING ? def : v->value.v_string.ptr);
}

const_string_s *
kvGetStringSByPath(kv_s * root, const char * path, const_string_s * def){
	kv_s * v = kvGetByPath(root, path);
	return (!v || v->type != KV_STRING ? def : (const_string_s *)&(v->value.v_string));
}

const char *
kvGetJsonByPath(kv_s * root, const char * path, const char * def){
	kv_s * v = kvGetByPath(root, path);
	return (!v || v->type != KV_JSON ? def : v->value.v_json.ptr);
}

const_string_s *
kvGetJsonSByPath(kv_s * root, const char * path, const_string_s * def){
	kv_s * v = kvGetByPath(root, path);
	return (!v || v->type != KV_JSON ? def : (const_string_s *)&(v->value.v_json));
}

void *
kvGetFunctionByPath(kv_s * root, const char * path, void * def){
	kv_s * v = kvGetByPath(root, path);
	return (!v || v->type != KV_FUNCTION ? def : v->value.v_function);
}

void *
kvGetPointerByPath(kv_s * root, const char * path, void * def){
	kv_s * v = kvGetByPath(root, path);
	return (!v || v->type != KV_POINTER ? def : v->value.v_pointer.ptr);
}


/*
 * Получение KV или фатальная ошибка если KV не найден
 */
kv_s * 
kvGetRequire(kv_s * root, const char * path){
	kv_s * v = kvGetByPath(root, path);
	if(!v) FATAL_ERROR("ERROR: Required variable [%s] not found\n", path);
	return v;
}


/*
 * Получение KV опредленного типа или фатальная ошибка если KV не найден или тип данных не совпадает
 */
kv_s * 
kvGetRequireType(kv_s * root, const char * path, kv_t type){
	kv_s * v = kvGetByPath(root, path); 
	if(!v) FATAL_ERROR("ERROR: Required variable [%s] not found\n", path); 
	if(v->type != type) FATAL_ERROR("ERROR: Variable [%s] typed as [%s] but required type is [%s]\n", path, kvEchoType(v->type), kvEchoType(type));
	return v;
}


bool 
kvGetRequireBool(kv_s * root, const char * path){
	kv_s * v = kvGetRequireType(root, path, KV_BOOL);
	return v->value.v_bool;
}

int64_t
kvGetRequireInt(kv_s * root, const char * path){
	kv_s * v = kvGetRequireType(root, path, KV_INT);
	return v->value.v_int;
}

double
kvGetRequireDouble(kv_s * root, const char * path){
	kv_s * v = kvGetRequireType(root, path, KV_DOUBLE);
	return v->value.v_double;
}

const char *
kvGetRequireString(kv_s * root, const char * path){
	kv_s * v = kvGetRequireType(root, path, KV_STRING);
	return v->value.v_string.ptr;
}

const_string_s *
kvGetRequireStringS(kv_s * root, const char * path){
	kv_s * v = kvGetRequireType(root, path, KV_STRING);
	return (const_string_s *)&(v->value.v_string);
}

const char *
kvGetRequireJson(kv_s * root, const char * path){
	kv_s * v = kvGetRequireType(root, path, KV_JSON);
	return v->value.v_json.ptr;
}

const_string_s *
kvGetRequireJsonS(kv_s * root, const char * path){
	kv_s * v = kvGetRequireType(root, path, KV_JSON);
	return (const_string_s *)&(v->value.v_json);
}

void *
kvGetRequireFunction(kv_s * root, const char * path){
	kv_s * v = kvGetRequireType(root, path, KV_FUNCTION);
	return v->value.v_function;
}

void *
kvGetRequirePointer(kv_s * root, const char * path){
	kv_s * v = kvGetRequireType(root, path, KV_POINTER);
	return v->value.v_pointer.ptr;
}



/*
 * Создает KV по указанному пути и возвращает указатель на него
 * Путь выглядит как для файловой системы, пример: 
 * settings/general/language/default
 */
kv_s *
kvSetByPath(kv_s * root, const char * path){
	if(!path) return root;
	kv_s * node = root;
	uint32_t n;
	while(*path == '/')path++;
	const char * ptr = path;
	const char * key = path;
	while(*ptr){
		while(*ptr && *ptr!='/') ptr++;
		n = ptr - key;
		if(n > 0) node = kvAppend(node, key, n, KV_REPLACE);
		//Если последний символ "/" то считаем что задан индекс директории index
		if(*ptr=='/' && !*(ptr+1)){
			return kvAppend(node, "index", 5, KV_REPLACE);
		}
		if(!*ptr || !*(ptr+1)) return node;
		ptr++;
		key = ptr;
	}
	return node;
}//END: kvSetByPath


inline kv_s *
kvSetNullByPath(kv_s * root, const char * path){
	return kvSetNull(kvSetByPath(root, path));
}

inline kv_s *
kvSetBoolByPath(kv_s * root, const char * path, bool value){
	return kvSetBool(kvSetByPath(root, path), value);
}

inline kv_s *
kvSetIntByPath(kv_s * root, const char * path, int64_t value){
	return kvSetInt(kvSetByPath(root, path), value);
}

inline kv_s *
kvSetDoubleByPath(kv_s * root, const char * path, double value){
	return kvSetDouble(kvSetByPath(root, path), value);
}

inline kv_s *
kvSetStringByPath(kv_s * root, const char * path, const char * str, uint32_t len){
	return kvSetString(kvSetByPath(root, path), str, len);
}

inline kv_s *
kvSetStringPtrByPath(kv_s * root, const char * path, char * str, uint32_t len){
	return kvSetStringPtr(kvSetByPath(root, path), str, len);
}

inline kv_s *
kvSetJsonByPath(kv_s * root, const char * path, const char * str, uint32_t len){
	return kvSetJson(kvSetByPath(root, path), str, len);
}

inline kv_s *
kvSetJsonPtrByPath(kv_s * root, const char * path, char * str, uint32_t len){
	return kvSetJsonPtr(kvSetByPath(root, path), str, len);
}

inline kv_s *
kvSetFunctionByPath(kv_s * root, const char * path, void * value){
	return kvSetFunction(kvSetByPath(root, path), value);
}

inline kv_s *
kvSetPointerByPath(kv_s * root, const char * path, void * ptr, v_pointer_free_cb cb){
	return kvSetPointer(kvSetByPath(root, path), ptr, cb);
}


inline kv_s *
kvSetDatetimeByPath(kv_s * root, const char * path, time_t time, const char * format){
	return kvSetDatetime(kvSetByPath(root, path), time, format);
}




/***********************************************************************
 * Поиск в массиве KV по значениям
 **********************************************************************/


/*
 * Ищет KV с указанным значением в родительской ноде
 */
kv_s *
kvInArrayNull(kv_s * parent){
	if(!parent) return NULL;
	if(parent->type != KV_OBJECT && parent->type != KV_ARRAY) return NULL;
	kv_s * node;
	for(node = parent->value.v_list.first; node != NULL; node = node->next){
		if(node->type == KV_NULL) return node;
	}
	return NULL;
}//END: kvInArrayNull



/*
 * Ищет KV с указанным значением в родительской ноде
 */
kv_s *
kvInArrayBool(kv_s * parent, bool term){
	if(!parent) return NULL;
	if(parent->type != KV_OBJECT && parent->type != KV_ARRAY) return NULL;
	kv_s * node;
	for(node = parent->value.v_list.first; node != NULL; node = node->next){
		if(node->type == KV_BOOL && node->value.v_bool == term) return node;
	}
	return NULL;
}//END: kvInArrayBool



/*
 * Ищет KV с указанным значением в родительской ноде
 */
kv_s *
kvInArrayInt(kv_s * parent, int64_t term){
	if(!parent) return NULL;
	if(parent->type != KV_OBJECT && parent->type != KV_ARRAY) return NULL;
	kv_s * node;
	for(node = parent->value.v_list.first; node != NULL; node = node->next){
		if(node->type == KV_INT && node->value.v_int == term) return node;
	}
	return NULL;
}//END: kvInArrayInt



/*
 * Ищет KV с указанным значением в родительской ноде
 */
kv_s *
kvInArrayDouble(kv_s * parent, double term){
	if(!parent) return NULL;
	if(parent->type != KV_OBJECT && parent->type != KV_ARRAY) return NULL;
	kv_s * node;
	for(node = parent->value.v_list.first; node != NULL; node = node->next){
		if(node->type == KV_DOUBLE && node->value.v_double == term) return node;
	}
	return NULL;
}//END: kvInArrayDouble


/*
 * Ищет KV с указанным значением в родительской ноде
 */
kv_s *
kvInArrayString(kv_s * parent, const char * term){
	if(!parent) return NULL;
	if(parent->type != KV_OBJECT && parent->type != KV_ARRAY) return NULL;
	kv_s * node;
	for(node = parent->value.v_list.first; node != NULL; node = node->next){
		if((node->type == KV_STRING || node->type == KV_JSON) && stringCompareCase(node->value.v_string.ptr, term)) return node;
	}
	return NULL;
}//END: kvInArrayString



/*
 * Ищет KV с указанным значением в родительской ноде
 */
kv_s *
kvInArrayPointer(kv_s * parent, void * term){
	if(!parent) return NULL;
	if(parent->type != KV_OBJECT && parent->type != KV_ARRAY) return NULL;
	kv_s * node;
	for(node = parent->value.v_list.first; node != NULL; node = node->next){
		if(node->type == KV_POINTER && node->value.v_pointer.ptr == term) return node;
	}
	return NULL;
}//END: kvInArrayPointer












