/***********************************************************************
 * XG SERVER
 * core/darray.h
 * Динамический массив
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   

#include "core.h"
#include "darray.h"



/**
 * Инициализация darray.c
 */
initialization(darray_c){
	/*
	printf("sizeof(darray_s) = %u\n",(uint32_t)sizeof(darray_s));
	darray_s * da = darrayCreate(DATYPE_STRING, 8,8);
	darraySetFree(da, mFree);

	char * str = NULL;
	int i;
	for(i=0;i<99;i++){
		str = stringRandom(8,NULL);
		darrayPush(da, str);
	}
	for(i=0;i<99;i++){
		printf("%i:\t%s\n",i,darrayGetChar(da,i,NULL));
	}
	darrayFree(da);
	*/
	DEBUG_MSG("darray.c initialized.");
}//END: initialization


/***********************************************************************
 * Функции
 **********************************************************************/


/*
 * Создание массива
 * type - тип данных в массиве
 * allocated - начальное количество элементов в массиве
 * increment - инкрементальное увеличение количества элементов
 */
darray_s *
darrayCreate(darray_type_e type, uint32_t allocated, uint32_t increment){
	darray_s * da	= (darray_s *)mNewZ(sizeof(darray_s));
	da->type		= type;
	da->allocated	= (!allocated ? 256 : allocated);
	da->increment	= (!increment ? 256 : increment);
	da->array		= (darray_data_u *)mNewZ(sizeof(darray_data_u) * allocated);
	return da;
}//END: darrayCreate


/*
 * Освобождение памяти, занятой под массив
 */
void
darrayFree(darray_s * da){
	if(!da) return;
	if(da->ffree != NULL && (da->type == DATYPE_POINTER || da->type == DATYPE_STRING)){
		uint32_t i;
		for(i=0;i<da->count;i++){
			if(da->array[i].v_ptr) da->ffree(da->array[i].v_ptr);
		}
	}
	mFree(da->array);
	mFree(da);
}//END: darrayFree



/*
 * Изменение значения инкремента
 */
darray_s *
darraySetIncrement(darray_s * da, uint32_t increment){
	da->increment = (increment > 0 ? increment : 256);
	return da;
}//END: darraySetIncrement



/*
 * Изменение значения инкремента
 */
darray_s *
darraySetOffset(darray_s * da, uint32_t offset){
	da->offset = (offset > 0 ? offset : 0);
	return da;
}//END: darraySetOffset



/*
 * Задает функцию освобождения памяти для элементов массива
 */
darray_s *
darraySetFree(darray_s * da, free_cb ffree){
	da->ffree = ffree;
	return da;
}//END: darraySetFree



/*
 * Преобразует ID в индекс массива
 */
inline int32_t
darrayId2Index(darray_s * da, uint32_t id){
	if(da->offset > id) return -1;
	return (int32_t)(id - da->offset);
}//END: darrayId2Index



/*
 * Преобразует индекс массива в ID
 */
inline uint32_t
darrayIndex2Id(darray_s * da, uint32_t index){
	return (uint32_t)(index + da->offset);
}//END: darrayIndex2Id



/*
 * Устанавливает значение для указанного ID в пределах от 0 до da->count
 */
darray_data_u *
darraySet(darray_s * da, uint32_t id, void * data){
	int32_t index = darrayId2Index(da, id);
	if(index < 0 || index >= da->count) return NULL;
	darray_data_u * item = &da->array[index];
	if(data){
		switch(da->type){
			case DATYPE_POINTER: 
				if(item->v_ptr != NULL && da->ffree != NULL) da->ffree(item->v_ptr);
				item->v_ptr = data;
			break;
			case DATYPE_STRING:
				if(item->v_ptr != NULL && da->ffree != NULL) da->ffree(item->v_ptr);
				item->v_char = (char *)data;
			break;
			case DATYPE_DOUBLE:
				item->v_double = *(double *)data;
			break;
			case DATYPE_INTEGER:
				item->v_int = *(int64_t *)data;
			break;
		}
	}
	return item;
}//END: darraySet



/*
 * Увеличивает размер массива до указанного ID и задает его значение
 */
darray_data_u *
darraySetId(darray_s * da, uint32_t id, void * data){
	int32_t index = darrayId2Index(da, id);
	if(index < 0) return NULL;
	if(index >= da->allocated){
		while(index >= da->allocated) da->allocated += da->increment;
		da->array = mResize(da->array, da->allocated * sizeof(darray_data_u));
		memset(&da->array[da->count], '\0', (da->allocated - da->count) * sizeof(darray_data_u));
	}
	da->count = index + 1;
	return darraySet(da, id, data);
}//END: darraySetId




/*
 * Добавляет новый элемент в конец массива
 */
darray_data_u *
darrayPush(darray_s * da, void * data){
	return darraySetId(da, darrayIndex2Id(da, da->count), data);
}//END: darrayPush


/*
 * Возарвщвет указатель на элемент массива по ID
 */
inline darray_data_u *
darrayGet(darray_s * da, uint32_t id){
	int32_t index = darrayId2Index(da, id);
	if(index < 0 || index >= da->count) return NULL;
	return &da->array[index];
}//END: darrayGet


inline int64_t
darrayGetInt(darray_s * da, uint32_t id, int64_t def){
	if(da->type != DATYPE_INTEGER) return def;
	int32_t index = darrayId2Index(da, id);
	if(index < 0 || index >= da->count) return def;
	return da->array[index].v_int;
}//END: darrayGetInt64

inline double
darrayGetDouble(darray_s * da, uint32_t id, double def){
	if(da->type != DATYPE_DOUBLE) return def;
	int32_t index = darrayId2Index(da, id);
	if(index < 0 || index >= da->count) return def;
	return da->array[index].v_double;
}//END: darrayGetDouble


inline char *
darrayGetChar(darray_s * da, uint32_t id, char * def){
	if(da->type != DATYPE_STRING) return def;
	int32_t index = darrayId2Index(da, id);
	if(index < 0 || index >= da->count) return def;
	return da->array[index].v_char;
}//END: darrayGetChar


inline void *
darrayGetPointer(darray_s * da, uint32_t id, void * def){
	if(da->type != DATYPE_POINTER) return def;
	int32_t index = darrayId2Index(da, id);
	if(index < 0 || index >= da->count) return def;
	return da->array[index].v_ptr;
}//END: darrayGetPointer






