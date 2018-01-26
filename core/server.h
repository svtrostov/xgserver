/***********************************************************************
 * XG SERVER
 * core.h
 * Заголовки ядра
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/


#ifndef _XGSERVER_H
#define _XGSERVER_H

#ifdef __cplusplus
extern "C" {
#endif


/***********************************************************************
 * Подключаемые заголовки
 **********************************************************************/
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <resolv.h>
#include <signal.h>

#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "core.h"
#include "kv.h"
#include "session.h"
#include "db.h"


#define DEMO_HTTP_RESPONSE \
	"<h2>HTTPS Test Server based on OpenSSL</h2>\r\n" \
	"<p>Successful connection</p>\r\n"\
	"<p>Hi TrueCisa man, how are you? ;)</p>\r\n"\
	"<form action=\"/test.php?q=test\" method=\"post\" enctype=\"multipart/form-data\">"\
	"  <p><input type=\"text\" name=\"str1\" value=\"str1 string\">"\
	"  <p><input type=\"text\" name=\"str2\" value=\"str2 string\">"\
	"  <p><input type=\"file\" name=\"file1\">"\
	"  <p><input type=\"file\" name=\"file2\">"\
	"  <p><button type=\"submit\">Submit</button>"\
	"</form> "


#if defined(USE_EPOLL)
	#include <sys/epoll.h>
	#define FDPOLL_IN		EPOLLIN
	#define FDPOLL_OUT	EPOLLOUT
	#define FDPOLL_HUP	EPOLLHUP
	#define FDPOLL_ERR	EPOLLERR
#else
	#include <sys/poll.h>
	#ifndef USE_POLL
		#define USE_POLL
	#endif
	#define FDPOLL_IN		POLLIN
	#define FDPOLL_OUT	POLLOUT
	#define FDPOLL_HUP	POLLHUP
	#define FDPOLL_ERR	POLLERR
#endif

#define FDPOLL_ERROR	(FDPOLL_HUP | FDPOLL_ERR)
#define FDPOLL_READ	(FDPOLL_IN  | FDPOLL_ERROR)
#define FDPOLL_WRITE	(FDPOLL_OUT | FDPOLL_ERROR)

//Макрос добавления буфера в вывод
#define echoBuffer(connection, buffer) do{ chunkqueueAddBuffer(connection->response.content, buffer, 0, buffer->count, true); }while(0)


/***********************************************************************
 * Константы
 **********************************************************************/

//Количество рабочих потоков
static const uint32_t server_worker_threads = 4;

//Размер стека рабочего потока
static const uint32_t worker_thread_stack_size = 1024 * 512;

//Максимальное количество дескрипторов
static const uint32_t server_max_fds = FD_SETSIZE;

//Максимальное количество одновременных соединенй
static const uint32_t server_max_connections = FD_SETSIZE;

//Максимальное время ожидания первого байта данных от клиента (в секундах, считается от начала установки соединения)
static const uint32_t accepting_read_timeout = 3;

//Максимальное время ожидания первого байта данных от клиента (в секундах, считается от начала установки соединения)
static const uint32_t handstake_timeout = 6;

//Время ожидания данных от клиента перед непосредственным закрытием соединения (в секундах)
static const uint32_t linger_on_close_timeout = 5;

//Размер инкремента для буфера входных данных от клиента: connection->request.data->increment
static const uint32_t request_buffer_increment = 1024 * 8; //по умолчанию 8 килобайт

//Размер инкремента для буфера выходных данных от сервера: connection->response.head->increment
static const uint32_t response_buffer_head_increment = 2048; //по умолчанию 2 килобайт

//Размер инкремента для буфера выходных данных от сервера: connection->response.body->increment
static const uint32_t response_buffer_body_increment = 1024 * 8; //по умолчанию 8 килобайт

//Размер списка заданий (равен максимальному количеству соединений x 4)
static const uint32_t server_joblist_size = FD_SETSIZE;

//Максимальная длинна маршрута (символов = байт), получаемая при запросе
static const uint32_t request_path_max = 512;

//Размер внутреннего буфера отправки данных из локальных файлов (примеряется в chunkqueue_s)
static const uint32_t chunkqueue_internal_buffer_size = 1024 * 32;


/***********************************************************************
 * Объявления и декларации
 **********************************************************************/



#define CLEAR_SSL_ERRORS \
	do { \
		unsigned long openssl_error; \
		while ((openssl_error = ERR_get_error())) { \
		} \
	} while(0)


//Сокет
typedef int socket_t;
#ifndef __socklen_t_defined
typedef int socklen_t;
#endif


typedef enum type_socket_type_e{
	SOCKTYPE_IPV4 = 0,
	SOCKTYPE_IPV6
} socket_type_e;


//HTTP методы
typedef enum type_request_method_e{
	HTTP_UNDEFINED = 0,		//Метод не задан
	HTTP_GET,				//GET
	HTTP_POST				//POST
} request_method_e;



//Стадии жизненнгого цикла соединения (описывает в каком состоянии в настоящий момент находится соединение)
typedef enum{

	//Начальная стадия
	CON_STAGE_NONE				= 0,	//В текущий момент данное соединение не используется

	//Установка соединения
	CON_STAGE_ACCEPTING			= CON_STAGE_NONE + 1,			//Принимаем соединение для обработки
	CON_STAGE_HANDSTAKE			= CON_STAGE_ACCEPTING + 1,		//SSL Соединение было только что принято для обработки, ожидается "рукопожатие"
	CON_STAGE_CONNECTED			= CON_STAGE_HANDSTAKE + 1,		//Соединение было только что принято для обработки

	//Чтение данных запроса
	CON_STAGE_READ				= CON_STAGE_CONNECTED + 1,		//Получение данных от клиента

	//Подготовка и отправка ответа 
	CON_STAGE_WORKING			= CON_STAGE_READ + 1,			//Обработка запроса сервером и формирование ответа
	CON_STAGE_BEFORE_WRITE		= CON_STAGE_WORKING + 1,		//Этап непосредственно перед началом отправки ответа клиенту
	CON_STAGE_WRITE				= CON_STAGE_BEFORE_WRITE + 1,	//Отправка ответа клиенту

	//Успешное завершение соединения
	CON_STAGE_COMPLETE			= CON_STAGE_WRITE + 1,			//Запрос был получен, успешно обработан, завершающая стадия обработки запроса

	//Ошибки обработки соединения
	CON_STAGE_ERROR				= CON_STAGE_COMPLETE + 1,	//Возникла ошибка при работе в процессе соединения (не связанная с сокетом)
	CON_STAGE_SOCKET_ERROR		= CON_STAGE_ERROR + 1,			//Возникла ошибка при работе с сокетом в процессе соединения (установка соединения, чтение, запись и т.д.)

	//Закрытие соединения
	CON_STAGE_CLOSE				= CON_STAGE_SOCKET_ERROR + 1,	//Соединение находится на стадии закрытия и должно быть закрыто

	//После закрытия соединения
	CON_STAGE_CLOSED			= CON_STAGE_CLOSE + 1,			//Соединение закрыто
	CON_STAGE_DESTROYING		= CON_STAGE_CLOSED + 1			//Соединение на стадии уничтожения
} connection_stage_e;



//Ошибки в процессе обработки соединения
typedef enum{

	CON_ERROR_NONE = 0,					//Нет ошибок
	CON_ERROR_DISCONNECT,				//Ошибка возникает когда соединение было неожиданно разорвано
	CON_ERROR_TIMEOUT,					//Ошибка возникает при превышении лимитов ожидания данных от клиента
	CON_ERROR_UNDEFINED_STAGE,			//Ошибка возникает когда соединение имеет неизвестный этап жизненного цикла

	CON_ERROR_ACCEPT_REQUEST,			//Ошибка приема соединения: возникает когда включен SSL режим и первый байт запроса не 0x80 (SSLv2) и не 0x16 (SSLv3/TLSv1) или наоброт
	CON_ERROR_ACCEPT_SOCKET,			//Ошибка приема соединения: возникает когда во время чтения первого байта из сокета возникла ошибка сокета
	CON_ERROR_ACCEPT_TIMEOUT,			//Ошибка приема соединения: возникает когда превышен интервал ожидания чтения первого байта запроса от клиента, заданный в accepting_read_timeout

	CON_ERROR_HANDSTAKE_SSL_CREATE,		//Ошибка SSL рукопожатия: возникает когда серверу не удается создать новый экземпляр структуры SSL соединения
	CON_ERROR_HANDSTAKE_SSL_SET_FD,		//Ошибка SSL рукопожатия: возникает когда серверу не удается установить сокет клиента для SSL соединения (SSL_set_fd(...) != 1)
	CON_ERROR_HANDSTAKE_SOCKET,			//Ошибка SSL рукопожатия: возникает когда во время рукопожатия произошла ошибка сокета
	CON_ERROR_HANDSTAKE_TIMEOUT,		//Ошибка SSL рукопожатия: возникает когда превышен интервал ожидания рукопожатия, заданный в handstake_timeout

	CON_ERROR_READ_SOCKET,				//Ошибка чтения из сокета: возникает когда во время чтения из сокета возникла ошибка сокета
	CON_ERROR_WRITE_SOCKET,				//Ошибка записи в сокет: возникает когда во время записи в сокет возникла ошибка сокета

	CON_ERROR_FDEVENT_SOCKET,			//Ошибка poll engine: возникает когда при вызове poll() на сокете возникло событие ошибки сокета
	CON_ERROR_FDEVENT_UNDEFINED,		//Ошибка poll engine: возникает когда при вызове poll() на сокете возникло неизвестное событие, отличное от чтение/запись/разрыв соединения/ошибка сокета

} connection_error_e;




//Результат работы функции
typedef enum{
	RESULT_ERROR	= -1,	//Прочие ошибки
	RESULT_OK		= 0,	//Нет ошибки
	RESULT_AGAIN	= 1,	//Требуется повторный вызов функции
	RESULT_CONRESET	= 2,	//Соединение разорвано
	RESULT_EOF		= 3,	//Все данные получены
	RESULT_TIMEOUT	= 4,	//Превышен интервал ожидания
	RESULT_COMPLETE	= 5		//Нет ошибки -операция завершена успешно
} result_e;


//Версия используемого HTTP протокола
typedef enum {
	HTTP_VERSION_UNDEFINED	= 0,	//HTTP/?.?
	HTTP_VERSION_1_0		= 10,	//HTTP/1.0
	HTTP_VERSION_1_1		= 11	//HTTP/1.1
} http_version_e;


//Методы обработки POST запроса
typedef enum type_post_method_e{
	POST_UNDEFINED = 0,		//Неизвестный /неподдерживаемый метод POST
	POST_URLENCODED,		//application/x-www-form-urlencoded
	POST_MULTIPART			//multipart/form-data
} post_method_e;


//Статусы работы сервера
typedef enum type_server_status_e{
	XGS_STOPPED 		= 0,	//сервер остановлен
	XGS_LOADING_DATA 	= 1,	//Идет загрузка данных
	XGS_INITIALIZING 	= 2,	//сервер в процессе инициализации
	XGS_WORKING		 	= 3		//сервер запущен и работает
} server_status_e;


//Позиция запроса
typedef enum{
	IN_HEADERS	= 0,	//Позиция в заголовках
	IN_BODY				//Позиция в контенте
} request_in_e;


//HTTP заголовки
typedef enum{
	HEADER_CONNECTION		= BIT(1),
	HEADER_CONTENT_LENGTH	= BIT(2),
	HEADER_CONTENT_TYPE		= BIT(3),
	HEADER_COOKIE			= BIT(4),
	HEADER_RANGE			= BIT(5),
	HEADER_X_REQUESTED_WITH	= BIT(6),
	HEADER_HOST				= BIT(7),
	HEADER_IF_NONE_MATCH	= BIT(8),
	HEADER_USER_AGENT		= BIT(9),
	HEADER_REFERER			= BIT(10)
} header_e;


//Этапы обработки задания для соединения
typedef enum{
	JOB_STAGE_NONE		= 0,						//Соединение в настоящий момент не обрабатывается
	JOB_STAGE_WAITING	= JOB_STAGE_NONE + 1,		//Соединение в настоящий момент находится в списке работы и ожидает обработки 
	JOB_STAGE_WORKING	= JOB_STAGE_WAITING + 1,	//Соединение в настоящий момент обрабатывается рабочим потоком
	JOB_STAGE_WAITMAIN	= JOB_STAGE_WORKING + 1		//Соединение ожидает обработки основным потоком
} job_stage_e;


//Типы частей контента
typedef enum{
	CHUNK_NONE		= 0,	//Нет контента (не использовать, пропустить)
	CHUNK_FILE		= 1,	//Контент из файла
	CHUNK_BUFFER	= 2,	//Контент из буфера buffer_s
	CHUNK_STRING	= 3,	//Контент из структуры типа string_s
	CHUNK_HEAP		= 4		//Контент из области памяти char *
} chunktype_e;


//Статус обработки AJAX запроса
typedef enum{
	AJAX_STATUS_NONE = 0,	//Запрос не обработан
	AJAX_STATUS_SUCCESS,	//Запрос обработан успешно
	AJAX_STATUS_ERROR,		//Запрос обработан с ошибками
	AJAX_STATUS_RELOGIN		//Требуется повторная аутентификация клиента
}ajax_status_e;


//Тип вставки контента в HTML тег
typedef enum{
	CONTENT_APPEND_SET = 0,	//заменить содержимое родительского элемента заданным контентом
	CONTENT_APPEND_BEGIN,	//добавить в начало родительского элемента
	CONTENT_APPEND_END,		//добавить в конец родительского элемента
	CONTENT_APPEND_BEFORE,	//добавить перед родительским элементом
	CONTENT_APPEND_AFTER	//добавить после родительского элемента
}content_append_e;


//Тип сообщений в AJAX
typedef enum{
	AJAX_MESSAGE_INFO = 0,	//Информационное сообщение
	AJAX_MESSAGE_WARNING,	//Предупреждение
	AJAX_MESSAGE_ERROR,		//Ошибка
	AJAX_MESSAGE_SUCCESS	//Сообщение об успешном выполнении
}message_type_e;


//Тип отображения AJAX сообщения
typedef enum{
	AJAX_MESSAGE_HIDE = 0,	//Не отображать сообщение
	AJAX_MESSAGE_WINDOW,	//Обобразить сообщение в модальном окне
	AJAX_MESSAGE_HINT,		//Обобразить сообщение во всплывающей подсказке
	AJAX_MESSAGE_CLIENT		//Способ отображения сообщения выбирает клиентское приложение
}message_display_e;




//Типы внутренних заданий сервера
typedef enum{
	JOB_INTERNAL_SESSION_CLEANER = 0	//Тип задания: удаление просроченных сессий
}jobinternal_e;




/***********************************************************************
 * Структуры
 **********************************************************************/

//Предварительная декларация структур
typedef struct	type_server_options_s	server_options_s;	//Опции сервера
typedef struct	type_server_s 			server_s;			//Сервер
typedef struct	type_request_uri_s		request_uri_s;		//URI запроса
typedef struct	type_request_range_s	request_range_s;	//Запрошенная часть файла
typedef struct	type_request_s			request_s;			//Запрос
typedef struct	type_response_s			response_s;			//Ответ
typedef struct	type_connection_s		connection_s;		//Клиентское соединение
typedef struct	type_fdevent_s			fdevent_s;			//Poll engine
typedef struct	type_fd_s				fd_s;				//Элемент дескриптора Fd
typedef struct	type_thread_s			thread_s;			//Элемент пула потоков
typedef struct	type_thread_pool_s		thread_pool_s;		//Пул потоков
typedef struct	type_joblist_s			joblist_s;			//Сипсок рабочих заданий для потоков
typedef struct	type_jobitem_s			jobitem_s;			//Элемент списка рабочих заданий
typedef struct	type_chunk_s			chunk_s;			//Часть контента
typedef struct	type_chunkqueue_s		chunkqueue_s;		//Очередь частей контента
typedef struct	type_ajax_s				ajax_s;				//Ajax ответ
typedef struct	type_jobinternal_s		jobinternal_s;		//Внутреннее задание для сервера


typedef result_e (*fdevent_handler)(server_s * srv, int revents, void * data);


//Структура сервера
typedef struct type_server_options_s{
	char 		* host;						//Прослушиваемый Хост
	int			port;						//Прослушиваемый порт (значения от 80 до 65000)
	int			max_head_size;				//Максимальный размер HTTP заголовков, принимаемых сервером, в байтах (по-умолчанию, 65536 байт = 64кб)
	int			max_post_size;				//Максимальный размер POST данных, принимаемых сервером, в байтах (по-умолчанию, 1048576 байт = 1Мб)
	int			max_upload_size;			//Максимальный размер загружаемого файла, принимаемого сервером, в байтах (по-умолчанию, 524288 байт = 500кб)
	int			max_read_idle;				//Маскимальное время ожидания данных от клиента (в секундах) -> максимальный интервал времени простоя между получениями данных от клиента (socket read)
	int			max_request_time;			//Маскимальное время запроса от клиента (в секундах) -> общее максимальное время ожидания сервером получения полного запроса от клиента
	string_s	public_html;				//Папка, содержащая открытый статичный контент (html, js, css, изобажения, видео и прочие файлы, которые не требуют обработки)
	string_s	private_html;				//Папка, содержащая закрытый статичный контент (html шаблоны, приватные изображения и т.д.)
	string_s	directory_index;			//Название файла по-умолчанию, если в URI запроса указана директория (последний символ URI = "/")
	char		* private_key_file;			//Путь к файлу закрытого ключа сервера в формате PEM "key.pem"
	char		* private_key_password;		//Пароль закрытого ключа сервера "key.pem"
	char		* certificate_file;			//Путь к файлу сертификата сервера  в формате PEM "cert.pem"
	char		* dh512_file;				//Путь к файлам DH параметров: openssl dhparam -out dh512.pem 512
	char		* dh1024_file;				//Путь к файлам DH параметров: openssl dhparam -out dh1024.pem 1024
	char		* dh2048_file;				//Путь к файлам DH параметров: openssl dhparam -out dh2048.pem 2048
	bool		use_ssl;					//Использовать SSL
	kv_s		* mimetypes;				//MIME Типы и расширения файлов
	const_string_s * default_mimetype;		//MIME тип по-умолчанию
	int			worker_threads;				//Количество рабочих потоков
} server_options_s;



//Структура парсинга поступившего запроса
//Просто объединение переменных в одно место
typedef struct type_request_parser_s{
	request_in_e	in;				//Позиция в запросе
	uint32_t		line_no;		//Текущий номер строки
	uint32_t		line_n;			//Начало строки (n символов от начала буффера)
	uint32_t		line_len;		//Длинна строки (n символов)
	uint32_t		ptr_n;			//Текущая позиция (n символов от начала буффера)
	uint32_t		body_n;			//Начало тела запроса (n символов от начала буффера)
	uint32_t		body_len;		//Длинна тела запроса (n символов)
}request_parser_s;



//Структура URL адреса
typedef struct type_request_uri_s{
	string_s uri;
	string_s path;
	string_s query;
	string_s fragment;
} request_uri_s;



//Структура информации о файле, полученном из POST запроса
typedef struct type_post_file_s{
	//Имя поля POST запроса хранится в структуре KV (con->request.files) и здесь не представлено
	const_string_s	filename;	//Имя файла					<- Содержит указатель на буфер запроса con->request.data->buffer
	const_string_s	mimetype;	//MIME тип файла			<- Содержит указатель на буфер запроса con->request.data->buffer
	const_string_s	content;	//Содержимое файла			<- Содержит указатель на буфер запроса con->request.data->buffer
} post_file_s;



//Структура информации о статичном файле на сервере
typedef struct type_static_file_s{
	/*
	struct stat {
		dev_t         st_dev;      // устройство 
		ino_t         st_ino;      // inode 
		mode_t        st_mode;     // режим доступа 
		nlink_t       st_nlink;    // количество жестких ссылок 
		uid_t         st_uid;      // идентификатор пользователя-владельца 
		gid_t         st_gid;      // идентификатор группы-владельца 
		dev_t         st_rdev;     // тип устройства (если это устройство) 
		off_t         st_size;     // общий размер в байтах 
		blksize_t     st_blksize;  // размер блока ввода-вывода в файловой системе 
		blkcnt_t      st_blocks;   // количество выделенных блоков 
		time_t        st_atime;    // время последнего доступа 
		time_t        st_mtime;    // время последнего изменения данных файла (контента)
		time_t        st_ctime;    // время последней модификации метаданных
	};
	*/
	struct stat		st;				//Информация по файлу
	string_s		* localfile;	//Полный путь и имя файла
	const_string_s	filename;		//Имя файла (указатель на начало имени файла из localfile)
	const_string_s	extension;		//Расширение файла (указатель на начало имени файла из localfile)
	const_string_s	mimetype;		//MIME тип (указатель MIME тип из массива MIME типов сервера server.config.mimetypes)
	int				fd;				//Дескриптор локального файла
	string_s		* etag;			//eTag
} static_file_s;



//Структура информации запрошенных диапазонах файла
typedef struct type_request_range_s{
	seek_position_e		seek;		//Откуда смещение (R_SEEK_START - от начала фала, R_SEEK_END - от конца файла)
	int64_t				begin_n;	//Начало блока (-1 = от начала/конца файла)
	int64_t				length;		//Количество байт для отправки
	request_range_s		* next;		//Следующий запрошенный диапазон файла
} request_range_s;




//Стркутура запроса
typedef struct type_request_s{
	request_parser_s	parser;				//Структура парсинга поступившего запроса (Просто объединение переменных в одно место)
	string_s			host;				//Запрашиваемый Host
	request_uri_s		uri;				//URI запроса
	buffer_s			* data;				//Буфер входящих данных запроса
	kv_s				* headers;			//Заголовки запроса
	size_t				headers_bits;		//Битовая матрица найденных заголовков set of enum header_e
	kv_s				* get;				//GET параметры
	kv_s				* post;				//POST параметры
	kv_s				* cookie;			//Cookie параметры
	kv_s				* files;			//Файлы, полученные от клиента в POST запросе
	request_range_s		* ranges;			//Информация о запрашиваемых диапазонах (частях) файла
	static_file_s		* static_file;		//Информация о запрошенном статичном файле
	const_string_s		if_none_match;		//Значение If-None-Match, полученное от клиента 
	const_string_s		user_agent;			//Значение User-Agent
	const_string_s		referer;			//Значение Referer
	request_method_e	request_method;		//Метод запроса: GET, POST
	http_version_e		http_version;		//Версия HTTP протокола клиента
	uint32_t			content_length;		//Длинна контента POST запроса (Значение Content-Length в заголовках)
	post_method_e		post_method;		//Метод обработки POST запроса (application/x-www-form-urlencoded или multipart/form-data)
	string_s			multipart_boundary;	//Граница при POST_MULTIPART (Content-Type: multipart/form-data; boundary=[xxxxxxxxxxxxx])
	bool				is_ajax;			//Признак, указывающий что запрос в AJAX формате (X-Requested-With: XMLHttpRequest)
} request_s;



//Стркутура ответа
typedef struct type_response_s{
	chunkqueue_s *		content;			//Очередь частей контента для отправки клиенту
	buffer_s			* head;				//Буфер исходящих данных ответа - заголовки ответа
	//buffer_s			* body;				//Буфер исходящих данных ответа - тело ответа
	kv_s				* headers;			//Заголовки ответа
	kv_s				* cookie;			//Новые cookies
} response_s;



//Элемент дескриптора Fd
typedef struct type_fd_s{
	socket_t			fd;					//Дескриптор соединения
	int					events;				//События
	int					poll_index;			//Индекс в массиве pollfds
	fdevent_handler		handler;			//Обработчик
	void				* data;				//Данные 
	void				* next;				//Данные для IDLE
} fd_s;


//Poll engine - обработка Poll
typedef struct type_fdevent_s{
	server_s			* server;			//Указатель на родительскую структуру server_s
	fd_s				** fds;				//Массив дескрипторов fd
#ifdef USE_POLL
	struct pollfd		* pollfds;			//Массив дескрипторов для poll
	uint32_t			pollfds_count;		//Количество дескрипторов
#endif
#ifdef USE_EPOLL
	int epoll_fd;
	struct epoll_event 	* epollfds;
#endif
} fdevent_s;



//Структура клиентского соединения
typedef struct type_connection_s{

	server_s			* server;			//Указатель на родительскую структуру server_s

	connection_stage_e	stage;				//Текущее состояние соединения
	socket_t			fd;					//Дескриптор текущего соединения
	int					index;				//Индекс текущего соединения в массиве соединений
	socket_addr_s		remote_addr;		//Адрес клиента
	int					http_code;			//HTTP статус обработки запроса (код ответа)

	SSL *				ssl;				//SSL соединение
	uint32_t 			renegotiations;		//Количество SSL "рукопожатий" перед установкой соединения

	request_s			request;			//Клиентский запрос
	response_s			response;			//Серверный ответ
	session_s			* session;			//Сессия клиента на текущем соединении

	ajax_s				* ajax;				//Структура AJAX ответа сервера

	time_t				start_ts;			//Время старта соединения
	time_t				read_idle_ts;		//Время начала простоя при выполнении операций чтения из сокета (в режиме ожидания данных)
	time_t				close_timeout_ts;	//Время начала закрытия сокета

	job_stage_e			job_stage;			//Состояние обработки соединения(не обрабатывается, находится в списке работ или обрабатывается) рабочим потоком
	jobitem_s			* job_item;			//Элемент в jobitem

	connection_error_e	connection_error;	//Номер последней ошибки, возникшей в процессе обработки соединения

	uint64_t			connection_id;		//Уникальный ID соединения

} connection_s;



//Структура сервера
typedef struct type_server_s{
	connection_s **		connections;		//Массив клиентских соединений
	uint32_t			connections_count;	//Количество занятых слотов (количество установленных соединений)

	fdevent_s *			fdevent;			//Poll engine

	thread_pool_s *		workers;			//Указатель на пул рабочих потоков
	joblist_s *			joblist;			//Указатель на список заданий для рабочих потоков

	joblist_s *			jobmain;			//Указатель на список рабочих заданий для основного потока

	time_t				current_ts;			//Текущее время сервера

	//Прослушиваемый сокет
	socket_type_e		socket_type;	//Тип прослушиваемо сокета
	socket_addr_s		addr;			//Адрес
	socket_t			listen_fd;		//Прослушиваемый сокет
	socket_t			pipe[2];	//Сокеты, события которых будут прерывать ожидание poll

	SSL_CTX 			* ctx;		//Указатель на SSL Context
	bool				stopped;	//Признак, указывающий что сервер остановлен и должен прекратить свою работу
	server_options_s	config;		//Настройки из конфигурационного файла

} server_s;



//Структура CRYPTO_dynlock_value, согласно требованиям OpenSSL, должна быть определна самоcтоятельно
struct CRYPTO_dynlock_value{
	pthread_mutex_t mutex;
};



//Структура элемента пула потоков
typedef struct type_thread_s{
	thread_pool_s	* pool;		//Указатель на пул потоков
	pthread_t		thread_id;	//Дескриптор потока
	size_t			index;		//Индекс потока в пуле потоков
	connection_s	* con;		//Указатель на обрабатываемое соединение
	bool 			destroy;	//Признак, указывающий что поток должен завершиться
} thread_s;



//Структура пула потоков
typedef struct type_thread_pool_s{
	server_s		* server;		//Указатель на уструктуру сервера, использующего пул потоков
	thread_s		** threads;		//Массив доступных потоков
	size_t			threads_count;	//Общее количество потоков
	size_t			threads_idle;	//Общее количество простаивающих потоков
	pthread_cond_t	condition;		//Условие
	pthread_mutex_t	mutex;			//Блокировка
} thread_pool_s;



//Структура элемента списка рабочих заданий
typedef struct type_jobitem_s{
	connection_s		* connection;	//Список соединений, ждущих обработки
	jobitem_s			* next;			//Следующий элемент
	bool				ignore;			//Признак, указывающий что данный элемент следует проигнорировать
} jobitem_s;



//Структура списка рабочих заданий
typedef struct type_joblist_s{
	server_s		* server;	//Указатель на уструктуру сервера, использующего список рабочих заданий
	jobitem_s		* first;	//Первый элемент списка рабочих заданий
	jobitem_s		* last;		//Последний элемент списка рабочих заданий
	jobitem_s		* idle;		//Свободные элементы списка рабочих заданий
} joblist_s;




//Структура части контента
typedef struct type_chunk_s{
	chunkqueue_s	* queue;		//Родительский список
	chunk_s			* prev;			//Предыдущая часть
	chunk_s			* next;			//Следующая часть
	chunktype_e		type;			//Тип части контента
	union{
		static_file_s	* file;		//Указатель на структуру локального файла из которого выполнять чтение
		buffer_s		* buffer;	//Указатель на буфер из которого выполнять чтение
		string_s		* string;	//Указатель на строку из которой выполнять чтение
		char			* heap;		//Указатель на область памяти из которой выполнять чтение
	};
	uint32_t		offset;	//Отступ от начала строки / буфера / файла
	uint32_t		length;	//Длинна читаемых данных
	bool			free;	//Признак, указывающий что при уничтожении структуры, следует также уничтожить данные, на которые идет ссылка
}chunk_s;




//Структура очереди частей контента
typedef struct type_chunkqueue_s{
	chunk_s			* first;	//Указатель на первую часть контента
	chunk_s			* last;		//Указатель на последнюю часть контента
	char			* temp;		//Внутренний буфер для отправки данных из файлов (устанавливается из chunkqueue_internal_buffer_size)
	uint32_t		temp_size;	//Общий объем данных во внутреннем буфере temp
	uint32_t		temp_n;		//Текущая позиция во внутреннем буфере n, откуда производить чтение
	struct{
		chunk_s		* chunk;	//Текущая часть
		uint32_t	written_n;	//Количество байт, отправленных в текущей части
	} current;
	uint32_t		content_length;	//Общая длинна контента
	chunkqueue_s	* next;		//для IDLE
}chunkqueue_s;



//Структура AJAX ответа
typedef struct type_ajax_s{
	connection_s	* connection;	//Указатель на обрабатываемое соединение
	kv_s			* root;			//Указатель на KV структуру ответа сервера
	kv_s			* action_kv;	//Указатель на KV структуру в root, содаржащую название действия, которое было запрошено со стороны клиента
	const char		* action;		//Указатель на название действия в action_kv, которое было запрошено со стороны клиента
	kv_s			* data_kv;		//Указатель на KV структуру в root, содаржащую произвольные данные для клиента - результаты обработки запроса
	kv_s			* stack_kv;		//Указатель на KV структуру в root, содаржащую cтек данных для обработки на клиенте
	kv_s			* title_kv;		//Указатель на KV структуру в root, содаржащую заголовок документа
	kv_s			* location_kv;	//Указатель на KV структуру в root, содаржащий URL для редиректа
	kv_s			* callback_kv;	//Указатель на KV структуру в root, содаржащую название функции JavaScript, которая должна быть вызвана по завершении запроса
	kv_s			* status_kv;	//Указатель на KV структуру в root, содаржащую статус обработки документа
	kv_s			* content_kv;	//Указатель на KV структуру в root, содаржащую массив HTML контента, отправляемого клиенту
	kv_s			* messages_kv;	//Указатель на KV структуру в root, содаржащую массив сообщений, отправляемых клиенту от сервера, возникших в процессе обработки запроса
	kv_s			* required_kv;	//Указатель на KV структуру в root, содаржащую массив подключаемых для страницы медиа-файлов
	kv_s			* debug_kv;		//Указатель на KV структуру в root, содаржащую массив отладочной информации
} ajax_s;



//Структура внутреннего задания сервера
typedef struct type_jobinternal_s{
	void			* data;			//Указатель на данные, необходимые для выполнения задания
	jobinternal_s	* next;			//для IDLE
	jobinternal_e	type;			//Тип внутреннего задания
	free_cb			free;			//Callback функция, вызываемая для уничтожения данных *data после использования
	bool			ignore;			//Признак, указывающий что данный элемент следует проигнорировать
} jobinternal_s;




/***********************************************************************
 * Функции: core/connection.c - соединения
 **********************************************************************/
connection_stage_e	connectionSetStage(connection_s * con, connection_stage_e new_stage);	//Устанавливает новый этап жизненного цикла соединения
job_stage_e			connectionSetJobStage(connection_s * con, job_stage_e new_stage, bool necessarily);	//Устанавливает новый этап обработки соединения рабочим потоком

result_e		connectionsCreate(server_s * srv);				//Создание массива структур клиентских соединений
result_e		connectionsFree(server_s * srv);				//Удаление массива структур клиентских соединений
void			connectionsPrint(server_s * srv);				//Вывод на экран состояния активных соединений
connection_s *	connectionGet(server_s * srv);					//Возвращает экземпляр доступного для использования клиентского соединения или NULL, если нет доступных соединений
uint64_t		connectionGetLastId(void);						//Возвращает последний ID соединения
result_e		connectionClear(connection_s * con);			//Сбрасывает структуру клиентского соединения до начальных параметров
result_e		connectionDelete(connection_s * con);			//"Удаляет" соединение (фактически структура не удаляется, а сбрасывается до начального состояния)
result_e		connectionClose(connection_s * con);			//Закрытие соединения
connection_s * 	connectionAccept(server_s * srv);				//Принимает клиентское соединение
const char *	connectionStageAsString(connection_stage_e s);	//Возвращает этап соединения в виде строки
const char *	connectionErrorAsString(connection_error_e e);	//Возвращает текстовое описание ошибки соединения
result_e		connectionEngine(connection_s * con);			//Обработка соединения согласно его текущего этапа жизненного цикла

result_e		connectionHandleRead(connection_s * con);		//Чтение данных от клиента
result_e		connectionHandleReadSSL(connection_s * con);	//Чтение SSL данных от клиента
result_e		connectionPrepareRequest(connection_s * con);	//Парсинг и обработка данных запроса
result_e		connectionHandleWrite(connection_s * con);		//Отправка данных клиенту
result_e		connectionHandleWriteSSL(connection_s * con);	//Отправка SSL данных клиенту
result_e		connectionHandleFdEvent(server_s * srv, int revents, void * data);	//Функция-обработчик события Poll Engine для клиентского сокета






/***********************************************************************
 * Функции: core/chunk.c - Работа с частями контента
 **********************************************************************/
chunkqueue_s *	chunkqueueCreate(void);		//Создание очереди частей контента
void 			chunkqueueFree(chunkqueue_s * cq);	//Удаление очереди
chunk_s *		chunkqueueAdd(chunkqueue_s * cq);	//Добавляет часть контента в очередь 
chunk_s *		chunkqueueAddFirst(chunkqueue_s * cq);	//Добавляет часть контента в начало очереди
chunk_s *		chunkqueueAddFile(chunkqueue_s * cq, static_file_s * sf, uint32_t offset, uint32_t length);	//Добавляет в очередь файл
chunk_s *		chunkqueueAddBuffer(chunkqueue_s * cq, buffer_s * buf, uint32_t offset, uint32_t length, bool vfree);	//Добавляет в очередь буфер
chunk_s *		chunkqueueAddString(chunkqueue_s * cq, string_s * s, uint32_t offset, uint32_t length, bool vfree);	//Добавляет в очередь строку
chunk_s *		chunkqueueAddHeap(chunkqueue_s * cq, char * ptr, uint32_t offset, uint32_t length, bool vfree);	//Добавляет в очередь указатель на область памяти
void			chunkqueueReset(chunkqueue_s * cq);	//Сбросить счетчик прочитанных данных и установить указатель в самое начало
inline bool		chunkqueueIsEmpty(chunkqueue_s * cq);	//Проверяет, пуста очередь или нет
result_e		chunkqueueRead(chunkqueue_s * cq, const char ** pointer, uint32_t * length);	//Читает из очереди очередную порцию контента для отправки клиенту
void			chunkqueueCommit(chunkqueue_s * cq, uint32_t length);	//Вызов функции "говорит" очереди о том, что было успешно отправлено length байт данных
chunk_s *		chunkqueueSetHeaderBuffer(chunkqueue_s * cq, buffer_s * buf, bool vfree);	//Устанавливает буфер с заголовками в начале очереди




/***********************************************************************
 * Функции: core/socket.c - Работа с сокетами
 **********************************************************************/
void		socketClose(socket_t sock_fd);	//Функция закрывает ранее открытый сокет
int			socketSetNonblockState(socket_t sock_fd, bool as_nonblock);	//Установка сокета в блокируемое (не блокируемое) состояние
result_e	socketReadPeek(socket_t sock_fd, char * buf, size_t ilen, uint32_t *olen);	//Чтение N байт из сокета с сохранением содержимого
result_e	socketRead(socket_t sock_fd, char * buf, size_t ilen, uint32_t *olen);	//Чтение N байт из сокета
result_e	socketWrite(socket_t sock_fd, const char * buf, size_t ilen, uint32_t *olen);	//Запись N байт в сокет



/***********************************************************************
 * Функции: core/ssl.c - Работа с OpenSSL
 **********************************************************************/
int			sslInit(server_s * srv);				//Инициализация OpenSSL
int			sslCleanup(void);						//Закрытие OpenSSL
SSL_CTX *	sslCtxInit(server_s * srv);				//Создание экземпляра SSL контекста
void		sslLoadCertificates(server_s * srv);	//Загрузка сертификатов
DH * 		sslDHLoad(const char * dh_file);		//Загрузка DH параметров
bool		sslThreadSetup(void);					//Инициализация потоков SSL
bool 		sslThreadCleanup(void);					//Уничтожение потоков SSL
result_e	sslDoHandshake(SSL * ssl);				//Проверка готовности SSL соединения
result_e	sslRead(SSL * ssl, char * buf, size_t buf_size, size_t * pcnt_read);	//Чтение данных из SSL
result_e	sslWrite(SSL * ssl, const char * buf, size_t buf_size, size_t * pcnt_write);	//Запись данных в SSL



/***********************************************************************
 * Функции: core/fdevent.c - Работа с Poll engine
 **********************************************************************/

fd_s *			fdNew(void);						//Создание элемента fd_s
inline void 	fdFree(fd_s *fdn);					//Освобождение элемента fd_s
fdevent_s *		fdeventNew(server_s * srv);			//Создание структуры FD events
void			fdeventFree(fdevent_s *ev);			//Освобождение структуры FD events
bool 			fdeventAdd(fdevent_s * ev, socket_t fd, fdevent_handler handler, void * data);	//Добавление сокета в Poll engine
bool			fdeventRemove(fdevent_s * ev, socket_t fd);	//Удаление сокета из Poll engine
int 			fdeventPoll(fdevent_s * ev, int timeout_ms);	//Получение событий сокетов
bool			fdEventSet(fdevent_s * ev, socket_t fd, int events);	//Добавление события для сокета
bool			fdEventDelete(fdevent_s * ev, socket_t fd);		//"Удаляет" события из сокета
socket_t		fdEventGetFd(fdevent_s *ev, size_t poll_index);	//Возвращает дескриптор сокета по индексу массива pollfds
int				fdEventGetNextIndex(fdevent_s * ev, int index);	//Возвращает следующий индекс массива pollfds, для дескриптора сокета которого есть события или -1, если ничего не найдено



/***********************************************************************
 * Функции: core/server.c - WEB сервер
 **********************************************************************/

//Работа с сервером
void			serverInit(void);								//Инициализация сервера
void			serverSetConfig(server_s * srv);				//Применяет опции конфигурации из webserver.conf к серверу
void			serverInitListener(server_s * srv);				//Инициализация прослушивающего сокета сервера



/***********************************************************************
 * Функции: core/threads.c - Работа с пулом рабочих потоков сервера
 **********************************************************************/

result_e		threadPoolCreate(server_s * server, size_t count);		//Создание пула потоков
result_e		threadPoolFree(thread_pool_s * pool);	//Завершение пула потоков
thread_s * 		threadCreate(thread_pool_s * pool);		//Создание нового потока
inline void		threadWakeup(thread_pool_s * pool);		//Посылает сигнал для "пробуждения" потока, т.к. появилось задание
result_e		threadConnectionEngine(connection_s * con);	//Обработка соединения рабочим потоком согласно его текущего статуса
mysql_s *		threadGetMysqlInstance(const char * instance_name);	//Получение экземпляра соединения с базой данных MySQL для текущего потока


/***********************************************************************
 * Функции: core/joblist.c - Работа со списком заданий
 **********************************************************************/

result_e		joblistCreate(server_s * srv);		//Создание списка рабочих заданий
result_e		joblistFree(joblist_s * joblist);	//Уничтожение списка рабочих заданий
void			jobAdd(connection_s * con);			//Добавление соединения в список заданий для обработки рабочим потоком
connection_s *	jobGet(joblist_s * joblist);		//Возвращает первое на очереди задание, одновременно удаляя его из списка заданий
bool			jobDelete(connection_s * con);		//Идаляет соединение из очереди рабочих заданий

result_e		jobmainCreate(server_s * srv);		//Создание списка заданий для основного потока
result_e		jobmainFree(joblist_s * jobmain);	//Уничтожение списка заданий
void			jobmainAdd(connection_s *con);		//Добавление соединения в список заданий для обработки основным потоком
connection_s *	jobmainGet(joblist_s * jobmain);	//Возвращает первое на очереди задание, одновременно удаляя его из списка заданий
bool			jobmainDelete(joblist_s * jobmain, connection_s * con);	//Ищет задание в очереди заданий и удаляет его

/***********************************************************************
 * Функции: core/request.c - Функции обработки HTTP запроса
 **********************************************************************/

//Работа с запросом request_s
inline void		requestFree(request_s * request);			//Освобождение структуры request_s
request_s * 	requestClear(request_s * request);			//Очистка структуры request_s
result_e		requestParseURI(request_uri_s * uri, const char * raw_uri, size_t ilen, const_string_s * directory_index);	//Парсинг URI адреса запроса в структуру request_uri_s
int				requestParseFirstLine(connection_s * con, const char * line, size_t len);	//Парсинг первой строки заголовков запроса GET /uri HTTP/x.y[\r\n], возвращает 0 в случае успеха или код HTTP ошибки
int				requestParseHeaderLine(connection_s * con, const char * line, uint32_t len);	//Функция обрабатывает строку заголовка запроса, возвращает 0 в случае успеха или код HTTP ошибки
int				requestHeadersToVariables(connection_s * con);	//Обработка заголовков запроса в переменные соединения, возвращает 0 в случае успеха или код HTTP ошибки
kv_s * 			requestParseCookies(const char * cookies);	//Парсинг Cookie в структуру KV
request_range_s * requestParseHttpRanges(const char * ptr, int * error);	//Парсинг HTTP Range
void			requestHttpRangesPrint(request_range_s * ranges);	//Вывод на экран структуры request_range_s
result_e		requestParseMultipartForm(connection_s * con);	//Функция обрабатывает POST запрос multipart/form-data
result_e		requestParseUrlEncodedForm(connection_s * con);	//Функция обрабатывает POST запрос application/x-www-form-urlencoded
const char *	requestMethodString(request_method_e method);	//Функция возвращает текстовое описание метода запроса
const char *	requestGetHeader(connection_s * con, const char * header);	//Функция возвращает значение заголовка
const char *	requestGetGPC(connection_s * con, const char * name, const char * rv, uint32_t * olen);	//Получение значения переменной из массива GET POST или COOKIE, в зависимости от фильтра rv (по-умолчанию rv = "gpc")
post_file_s *	requestGetFile(connection_s * con, const char * name);	//Возвращает структуру, содержащую загруженный методом POST файл
static_file_s *	requestStaticFileInfo(connection_s * con);	//Пытается найти локально запрошенный файл, и если файл найден - возвращает информацию о нем
void			requestStaticFileFree(static_file_s * f);	//Освобождает память, занятую структурой статичного файла




/***********************************************************************
 * Функции: core/response.c - Функции обработки HTTP ответа
 **********************************************************************/

inline void		responseFree(response_s * response);		//Освобождение структуры response_s
response_s * 	responseClear(response_s * response);		//Очистка структуры response_s
const char *	responseCodeCode(int http_code);			//Функция возвращает текстовое представление кода HTTP ответа, согласно номеру ответа
const char *	responseCodeString(int http_code);			//Функция возвращает текстовое описание кода HTTP ответа, согласно номеру ответа
const char *	responseHTTPVersionString(http_version_e v);//Функция возвращает текстовое описание HTTP версии используемого протокола
void			responseHttpLocation(connection_s * con, const char * location, bool temporarily);	//Перенаправление
void			responseHttpError(connection_s * con);		//Подготовка ответа ошибки сервера
bool			responseSetCookie(response_s * response, const char * key_name, const char * value);	//Добавляет Cookie в ответ сервера
bool			responseSetHeader(response_s * response, const char * header, const char * value, kv_rewrite_rule rewrite);	//Добавляет заголовок в ответ сервера

void			responseBuildFirstLine(buffer_s * buffer, int http_code, http_version_e http_version);	//Подготавливает первую строку ответа сервера
void			responseBuildHeaderLines(buffer_s * buffer, kv_s * headers);	//Генерация заголовков ответа сервера
void			responseBuildCookieLines(buffer_s * buffer, kv_s * cookie);	//Генерация заголовков Cookie в ответ сервера
void			responseBuildHeaders(connection_s * con);	//Генерация заголовка ответа сервера

result_e		responseStaticFile(connection_s * con, static_file_s * sf);	//Подготовка к отправке статичного файла клиенту



/***********************************************************************
 * Функции: core/route.c - Функции управления обработчиками запросов
 **********************************************************************/

//Callback функция для обработки запроса по маршруту
typedef result_e (*route_cb)(connection_s *);

bool				routeAdd(const char * path, route_cb v_function);	//Добавляет функцию-обработчик запроса для обработки определенного маршрута
route_cb			routeGet(const char * path);	//Ищет функцию-обработчик запроса для обработки определенного маршрута




/***********************************************************************
 * Функции: core/ajax.c - Функции AJAX ответа сервера
 **********************************************************************/

ajax_s *	ajaxNew(connection_s * con);	//Создание структуры AJAX ответа сервера для соединения
void		ajaxFree(ajax_s * ajax);		//Освобождение структуры AJAX 
void		ajaxSetTitle(ajax_s * ajax, const char * title, uint32_t title_len);	//Устанавливает заголовок документа
void		ajaxSetLocation(ajax_s * ajax, const char * url, uint32_t url_len);	//Устанавливает URL редиректа
void		ajaxSetCallback(ajax_s * ajax, const char * call, uint32_t call_len);	//Устанавливает функцию JavaScript, которая должна быть вызвана по завершении запроса
void		ajaxSetStatus(ajax_s * ajax, ajax_status_e status);	//Устанавливает cтатус обработки запроса
void		ajaxAddRequired(ajax_s * ajax, const char * url, uint32_t url_len, const char * call, uint32_t call_len);	//Добавление в массив подключаемых для страницы медиа-файлов нового элемента
void		ajaxRequiredClear(ajax_s * ajax);	//Очистка массива подключаемых для страницы медиа-файлов
void		ajaxAddContent(ajax_s * ajax, const char * parent, uint32_t parent_len, const char * content, uint32_t content_len, content_append_e append);	//Добавление HTML контента, возвращаемого через AJAX запрос
void		ajaxContentClear(ajax_s * ajax);	//Очистка массива контента
void		ajaxAddMessage(
				ajax_s * ajax, 
				const char * id, uint32_t id_len, 
				const char * title, uint32_t title_len, 
				const char * text, uint32_t text_len,
				message_type_e type,
				message_display_e display
			);	//Добавление сообщения в массив сообщений, отправляемых клиенту от сервера, возникших в процессе обработки запроса
void		ajaxMessagesClear(ajax_s * ajax);	//Очистка массива сообщений
void		ajaxSetStackKV(ajax_s * ajax, const char * name, uint32_t name_len, kv_s * kv);	//Добавление элемента в дополнительный стек данных для обработки на клиенте - KV
void		ajaxSetStackBool(ajax_s * ajax, const char * name, uint32_t name_len, bool value);	//Добавление элемента в дополнительный стек данных для обработки на клиенте - Bool
void		ajaxSetStackInt(ajax_s * ajax, const char * name, uint32_t name_len, int64_t value);	//Добавление элемента в дополнительный стек данных для обработки на клиенте - Int
void		ajaxSetStackDouble(ajax_s * ajax, const char * name, uint32_t name_len, double value);	//Добавление элемента в дополнительный стек данных для обработки на клиенте - Double
void		ajaxSetStackString(ajax_s * ajax, const char * name, uint32_t name_len, const char * str, uint32_t str_len);	//Добавление элемента в дополнительный стек данных для обработки на клиенте - String
void		ajaxSetStackStringPtr(ajax_s * ajax, const char * name, uint32_t name_len, char * str, uint32_t str_len);	//Добавление элемента в дополнительный стек данных для обработки на клиенте - String pointer
void		ajaxSetStackJson(ajax_s * ajax, const char * name, uint32_t name_len, const char * str, uint32_t str_len);	//Добавление элемента в дополнительный стек данных для обработки на клиенте - Json
void		ajaxSetStackJsonPtr(ajax_s * ajax, const char * name, uint32_t name_len, char * str, uint32_t str_len);	//Добавление элемента в дополнительный стек данных для обработки на клиенте - Json pointer
void		ajaxStackClear(ajax_s * ajax);	//Очистка стека
void		ajaxSetDataKV(ajax_s * ajax, const char * name, uint32_t name_len, kv_s * kv);	//Добавление элемента в данные для клиента - KV
void		ajaxSetDataBool(ajax_s * ajax, const char * name, uint32_t name_len, bool value);	//Добавление элемента в данные для клиента - Bool
void		ajaxSetDataInt(ajax_s * ajax, const char * name, uint32_t name_len, int64_t value);	//Добавление элемента в данные для клиента - Int
void		ajaxSetDataDouble(ajax_s * ajax, const char * name, uint32_t name_len, double value);	//Добавление элемента в данные для клиента - Double
void		ajaxSetDataString(ajax_s * ajax, const char * name, uint32_t name_len, const char * str, uint32_t str_len);	//Добавление элемента в данные для клиента - String
void		ajaxSetDataStringPtr(ajax_s * ajax, const char * name, uint32_t name_len, char * str, uint32_t str_len);	//Добавление элемента в данные для клиента - String pointer
void		ajaxSetDataJson(ajax_s * ajax, const char * name, uint32_t name_len, const char * str, uint32_t str_len);	//Добавление элемента в данные для клиента - Json
void		ajaxSetDataJsonPtr(ajax_s * ajax, const char * name, uint32_t name_len, char * str, uint32_t str_len);	//Добавление элемента в данные для клиента - Json pointer
void		ajaxDataClear(ajax_s * ajax);	//Очистка данных для клиента
void		ajaxAddDebug(ajax_s * ajax, const char * data, uint32_t data_len);	//Добавление в массив отладочной информации нового элемента
void		ajaxDebugClear(ajax_s * ajax);	//Очистка массива отладочной информации
void		ajaxError(
				ajax_s * ajax, 
				const char * id, uint32_t id_len, 
				const char * title, uint32_t title_len, 
				const char * text, uint32_t text_len
			);	//Помечает AJAX ответ как выполненный с ошибкой
void		ajaxSuccess(
				ajax_s * ajax, 
				const char * id, uint32_t id_len, 
				const char * title, uint32_t title_len, 
				const char * text, uint32_t text_len
			);	//Помечает AJAX ответ как выполненный успешно
void		ajaxResponse(ajax_s * ajax);	//Формирует AJAX ответ сервера и записывает его в очередь частей контента текущего соединения




/***********************************************************************
 * Функции: core/jobinternal.c - Работа со списком внутренних заданий сервера
 **********************************************************************/

void			jobinternalAdd(jobinternal_e type, void * data, free_cb cb);		//Добавление внутреннего рабочего задания для сервера
jobinternal_s *	jobinternalGet(void);	//Возвращает первое на очереди задание, одновременно удаляя его из списка заданий
inline void		jobinternalWakeup(void);	//Посылает сигнал для "пробуждения" потока, т.к. появилось задание
void			jobinternalThreadFree(void);	//Завершение рабочего потока сервера


#ifdef __cplusplus
}
#endif

#endif //_XGSERVER_H
