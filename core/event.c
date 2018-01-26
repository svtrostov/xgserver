/***********************************************************************
 * XG SERVER
 * core/event.c
 * Механизм обработки событий
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   

#include "core.h"
#include "event.h"
#include "darray.h"



//Инкремент идентификатора событий
//События от 0 до 99 зарезервированы для внутреннего использования
static uint32_t		_event_last_uid = 99;

//Массив событий
static darray_s		* _events = NULL;




/*EVENT TEST
static void _ev_user_1(evinfo_s * event){
	vprintf(event->data, event->args);
}

static void _ev_user_2(evinfo_s * event){
	vprintf(event->data, event->args);
}

static void _ev_user_3(evinfo_s * event){
	char * c = va_arg(event->args, char *);
	int i = va_arg(event->args, int);
	printf("%u of %u: %s = %d (call = %u)\n", event->index,event->count, c,i, event->calls);
	if(event->calls < 3) event->again = true;
	event->stop = false;
}
*/


/**
 * Инициализация event.c
 */
initialization(event_c){

	//Инициализация механизма событий
	eventInit();

/*
	uint32_t event_id = eventRegister();
	addListener(event_id, _ev_user_1, true, 0);
	addListener(event_id, _ev_user_2, true, 0);
	addListener(event_id, _ev_user_3, false, 1);
	fireEvent(event_id, "%s = %d\n", "test", 12345);
*/

	DEBUG_MSG("event.c initialized.");
}//END: initialization




/***********************************************************************
 * Функции
 **********************************************************************/

/*
 * Освобождение памяти, занятой под событие и его обработчики
 */
static void
_eventFree(void * ptr){
	if(!ptr) return;
	event_s * event = (event_s *)ptr;
	listener_s * item = event->first;
	listener_s * next = NULL;
	while(item){
		next = item->next;
		mFree(item);
		item = next;
	}
	mFree(event);
}//END: _eventFree


/*
 * Инициализация механизма событий
 */
void
eventInit(void){
	if(_events!=NULL) return;

	_events = darrayCreate(DATYPE_POINTER, 256, 256);
	darraySetFree(_events, _eventFree);

	//Регистрация системных событий
	eventCoreRegister(EVENT_LOADER_START);		//Событие функции main() перед началом выполнения каких либо действий
	eventCoreRegister(EVENT_LOADER_COMPLETE);	//Событие функции main() после загрузки настроек, инициализации нужных компонентов и перед стартом сервера
	eventCoreRegister(EVENT_SERVER_INIT);		//Событие возникает перед началом запуска сервера
	eventCoreRegister(EVENT_SERVER_START);		//Событие возникает после инициализации прослушивающего сокета, рабочих потов и т.д.
	eventCoreRegister(EVENT_SERVER_STOP);		//Событие возникает сразу после завершения приема клиентских соединений перед завершением работы сервера

}//END: eventInit


/*
 * Освобождение памяти, занятой под событие и его обработчики
 */
void
eventFree(void){
	if(!_events) return;
	darrayFree(_events);
	_events = NULL;
}//END: eventFree



/*
 * Регистрирует системное событие ядра под определенным идентификатором
 * В этой функции регистрируются события с ID от 0 до 99
 */
void
eventCoreRegister(uint32_t event_id){
	if(event_id > 99) return;
	eventInit();
	event_s * event = (event_s *)mNewZ(sizeof(event_s));
	darraySetId(_events, event_id, event);
	event->event_id = event_id;
	return;
}//END: eventCoreRegister



/*
 * Регистрирует событие и возвращает идентификатор события
 */
uint32_t
eventRegister(void){
	eventInit();
	_event_last_uid++;
	event_s * event = (event_s *)mNewZ(sizeof(event_s));
	darraySetId(_events, _event_last_uid, event);
	event->event_id = _event_last_uid;
	return _event_last_uid;
}//END: eventRegister



/*
 * Добавляет функцию-обработчик для указанного события
 * Возвращает количество идентичных функций-обработчиков для данного события
 * event_id - идентификатор события, присвоенный событию через вызов функции eventRegister
 * f - указатель на функцию-обработчик
 * ignore_if_exists - признак, указывающий что необходимо игнорировать вставку функции-обработчика, если она уже присутствует в событии
 */
uint32_t
addListener(uint32_t event_id, listener_cb f, bool ignore_if_exists, int priority){
	if(!f) return 0;
	eventInit();
	event_s * event = (event_s *)darrayGetPointer(_events, event_id, NULL);
	if(!event || event->event_id != event_id) RETURN_ERROR(0, "!event || event->event_id != event_id");
	uint32_t n = 0;
	listener_s * new;
	listener_s * item;
	//Если требуется проверка существования этого же экземпляра функции-обработчика для указанного события
	for(item = event->first; item != NULL; item = item->next){
		if(item->listener == f) n++;
	}
	if(ignore_if_exists && n > 0) return n;
	event->count++;
	new = (listener_s *)mNewZ(sizeof(listener_s));
	new->listener = f;
	new->event = event;
	new->priority = priority;
	if(!event->first){
		event->first = new;
		event->last = new;
		return (n+1);
	}{
		for(item = event->first; item != NULL; item = item->next){
			if(item->priority < priority){
				new->prev = item->prev;
				if(item->prev) item->prev->next = new;
				new->next = item;
				item->prev = new;
				if(event->first == item) event->first = new;
				return (n+1);
			}
		}
	}
	if(event->last){
		new->prev = event->last;
		event->last->next = new;
	}
	event->last = new;
	return (n+1);
}//END: addListener



/*
 * Удаляет функцию-обработчик для указанного события
 * Возвращает количество удаленных функций-обработчиков
 * event_id - идентификатор события, присвоенный событию через вызов функции eventRegister
 * f - указатель на функцию-обработчик, которую требуется удалить
 * count - количество удаляемых функций-обработчиков, 0 - удалить все функции-обработчики 
 */
uint32_t
removeListener(uint32_t event_id, listener_cb f, uint32_t count){
	if(!f) return 0;
	eventInit();
	event_s * event = (event_s *)darrayGetPointer(_events, event_id, NULL);
	if(!event || event->event_id != event_id) RETURN_ERROR(0, "!event || event->event_id != event_id");
	listener_s * item = event->first;
	listener_s * next = NULL;
	uint32_t n = 0;
	while(item && (!count || n < count)){
		next = item->next;
		if(item->listener == f){
			if(item->next)item->next->prev = item->prev;
			if(item->prev)item->prev->next = item->next;
			if(event->first == item) event->first = item->next;
			if(event->last == item) event->last = item->prev;
			mFree(item);
			n++;
			event->count--;
		}
		item = next;
	}
	return n;
}//END: removeListener



/*
 * Удаляет все функции-обработчики для данного события
 * Возвращает количество удаленных функций-обработчиков
 * event_id - идентификатор события, присвоенный событию через вызов функции eventRegister
 */
uint32_t
eventClear(uint32_t event_id){
	eventInit();
	event_s * event = (event_s *)darrayGetPointer(_events, event_id, NULL);
	if(!event || event->event_id != event_id) RETURN_ERROR(0, "!event || event->event_id != event_id");
	listener_s * item = event->first;
	listener_s * next = NULL;
	uint32_t n = 0;
	while(item){
		next = item->next;
		mFree(item);
		n++;
		item = next;
	}
	event->count = 0;
	event->first = NULL;
	event->last = NULL;
	return n;
}//END: eventClear



/*
 * Вызывает функции-обработчики для указанного события с передачей в них в качестве аргумента указателя data
 * Возвращает числовое значение, заданное в evinfo.result
 */
int
fireEvent(uint32_t event_id, void * data, ...){
	eventInit();
	event_s * event = (event_s *)darrayGetPointer(_events, event_id, NULL);
	if(!event || event->event_id != event_id) RETURN_ERROR(-1, "!event || event->event_id != event_id (%u)", event_id);
	listener_s * item;
	evinfo_s evinfo;
	uint32_t index = 0;
	va_list args;
	va_start(args, data);

	evinfo.event_id	= event_id;
	evinfo.data		= data;
	evinfo.count	= event->count;
	evinfo.result	= 0;
	evinfo.stop		= false;

	//Вызов функций-обработчиков для события
	for(item = event->first; item != NULL; item = item->next, index++){
		if(item->listener){
			evinfo.calls = 0;
			do{
				evinfo.calls++;
				va_copy(evinfo.args, args);
				evinfo.index	= index;
				evinfo.priority	= item->priority;
				evinfo.self		= item->listener;
				evinfo.again	= false;
				item->listener(&evinfo);
				va_end(evinfo.args);
			}while(evinfo.again);
			if(evinfo.stop) break;
		}
	}
	va_end(args);
	return evinfo.result;
}//END: fireEvent








