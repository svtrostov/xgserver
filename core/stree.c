/***********************************************************************
 * XG SERVER
 * core/stree.h
 * Текстовое дерево
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   

#include "core.h"
#include "stree.h"



/**
 * Инициализация stree.c
 */
initialization(stree_c){
/*
	#define _sadd(s) streeSet(stree, s, s, SNODE_REPLACE)
	#define _sdel(s) streeDelete(stree, s)
	printf("sizeof(snode_s) = %u\n",(uint32_t)sizeof(snode_s));
	stree_s * stree = streeNew(mFree);

	char * str = NULL;
	int i;
	for(i=0;i<99999;i++){
		str = stringRandom(8,NULL);
		streeSet(stree, str, str, SNODE_REPLACE | SNODE_FREE);
	}
	_sadd("terra");
	_sadd("test");
	_sadd("test1");
	_sadd("test2");
	_sadd("xxx");
	_sadd("xyx");
	_sadd("xyz");
	streePrint(stree);
	printf("----------------------------------\n");
	_sdel("test2");
	streePrint(stree);
	printf("----------------------------------\n");
	_sdel("test");
	_sdel("test1");
	_sdel("xxx");
	streePrint(stree);
	printf("----------------------------------\n");
*/
	//snode_s * node = streeGet(stree,str);
	//if(node != NULL) printf("node [%s] found, value is [%s]\n", str, (char *)node->data);
	//streePrint(stree);
	DEBUG_MSG("stree.c initialized.");
}//END: initialization


/***********************************************************************
 * Функции
 **********************************************************************/

/*
 * Печать дерева
 */
static void snodePrint(snode_s * snode, int lvl){
	int i;
	for(;snode != NULL; snode = snode->next){
		for(i=0;i<lvl;i++) printf("  ");
		printf("%c\n",snode->ch);
		snodePrint(snode->child, lvl+1);
		//printf("\n");
	}
}

void
streePrint(stree_s * stree){
	int i;
	for(i=0;i<=PRINT_INDEX_MAX;i++){
		if(stree->root[i]!=NULL) snodePrint(stree->root[i], 0);
	}
}//END: streePrint


/*
 * Создание дерева
 */
inline stree_s *
streeNew(free_cb ffree){
	stree_s * stree = (stree_s *)mNewZ(sizeof(stree_s));
	stree->ffree = ffree;
	return stree;
}//END: streeNew



/*
 * Создание ноды дерева
 */
inline snode_s *
snodeNew(u_char ch){
	snode_s * snode = (snode_s *)mNewZ(sizeof(snode_s));
	snode->ch = ch;
	return snode;
}//END: snodeNew



/*
 * Освобождение ноды дерева
 */
inline void
snodeFree(snode_s * snode){
	if(snode) mFree(snode);
}//END: snodeFree


/*
 * Извлекает ноду из дерева и возвращает указатель на нее
 */
inline snode_s *
snodeExtract(snode_s * snode){
	if(!snode) return NULL;
	snode_s * parent = snode->parent;
	if(!parent) return snode;
	if(parent->child == snode){
		parent->child = snode->next;
		snode->next = NULL;
		snode->parent = NULL;
		return snode;
	}else{
		snode_s * item;
		for(item = parent->child; item != NULL; item = item->next){
			if(item->next == snode){
				item->next = snode->next;
				snode->next = NULL;
				snode->parent = NULL;
			}
		}
	}
	return snode;
}//END: snodeExtract




/*
 * Добавляет в дерево новый элемент и возвращает указатель на него или NULL в случае ошибки
 */
snode_s *
streeSet(stree_s * stree, const char * key, void * data, u_char flags){
	if(!stree || !key) return NULL;
	register const u_char * ptr = (const u_char *)key;
	register const u_char * tmp = ptr;
	register int i = 0;
	while(*tmp && i < STREE_DEPTH){
		if(_print_index[*tmp++] < 0) return NULL;
	}
	if(*tmp) return NULL;
	i = _print_index[*ptr];
	register snode_s * parent;
	if( (parent = stree->root[i]) == NULL){
		parent = stree->root[i] = snodeNew(*ptr);
	}
	ptr++;
	register snode_s * node = NULL;
	while(*ptr){
		if(!parent->child){
			node = parent->child = snodeNew(*ptr);
			node->parent = parent;
		}else{
			for(node = parent->child; node != NULL; node = node->next){
				if(node->ch == *ptr) break;
			}
			if(!node){
				node = snodeNew(*ptr);
				node->parent = parent;
				node->next = parent->child;
				parent->child = node;
			}
		}
		parent = node;
		ptr++;
	}
	if(node){
		if(BIT_ISSET(node->flags, SNODE_REAL) && node->data != NULL){
			if(BIT_ISSET(node->flags, SNODE_REPLACE)){
				if(stree->ffree != NULL && BIT_ISSET(node->flags, SNODE_FREE)) stree->ffree(node->data);
				node->data = data;
				node->flags = flags;
				BIT_SET(node->flags, SNODE_REAL);
			}
		}else{
			node->data = data;
			node->flags = flags;
			BIT_SET(node->flags, SNODE_REAL);
		}
	}
	return node;
}//END: streeInsert




/*
 * Ищет в дереве ноду с указанным ключем и возвращает на нее указатель или NULL если нода не найдена
 */
snode_s *
streeGet(stree_s * stree, const char * key){
	if(!stree || !key) return NULL;
	register const u_char * ptr = (const u_char *)key;
	register int i = _print_index[*ptr++];
	if(i < 0) return NULL;
	register snode_s * parent;
	if((parent = stree->root[i]) == NULL) return NULL;
	register snode_s * node = NULL;
	//register int n = 0;
	while(*ptr){
		if(!parent->child) return NULL;

		for(node = parent->child; node != NULL; node = node->next){
			//n++;
			if(node->ch == *ptr) break;
		}
		if(!node)return NULL;
		parent = node;
		ptr++;
	}
	//printf("streeGet() n = %i\n",n);
	return (BIT_ISSET(node->flags, SNODE_REAL) ? node : NULL);
}//snodeSearch




/*
 * Удаляет ноду и данные ноды, если нода не используется
 */
void
snodeDelete(stree_s * stree, snode_s * snode){
	if(!snode) return;
	if(BIT_ISSET(snode->flags,SNODE_REAL) && snode->data != NULL && stree->ffree != NULL && BIT_ISSET(snode->flags, SNODE_FREE)){
		snode->data = NULL;
		stree->ffree(snode->data);
	}
	BIT_UNSET(snode->flags, SNODE_REAL);
	snode_s * parent = snode->parent;
	while(snode && snode->child == NULL && snode->parent != NULL && BIT_ISUNSET(snode->flags,SNODE_REAL)){
		parent = snode->parent;
		snodeFree(snodeExtract(snode));
		snode = parent;
	}
}//END: snodeDelete



/*
 * Ищет в дереве ноду с указанным ключем и удаляет ее
 */
void
streeDelete(stree_s * stree, const char * key){
	snode_s * node = streeGet(stree, key);
	if(!node) return;
	snodeDelete(stree, node);
	return;
}//END: streeDelete






