/***********************************************************************
 * XG SERVER
 * core/response.c
 * Функции работы с клиентским запросом response_s
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   

#include "core.h"
#include "server.h"




/***********************************************************************
 * Работа с ответом response_s
 **********************************************************************/ 

/*
 * Освобождение структуры response_s
 */
inline void
responseFree(response_s * response){
	mFree(responseClear(response));
}//END: responseFree



/*
 * Очистка структуры response_s
 */
response_s * 
responseClear(response_s * response){

	//Освобождение занятой памяти
	if(response->head)		bufferFree(response->head);
	if(response->headers)	kvFree(response->headers);
	if(response->cookie)	kvFree(response->cookie);
	if(response->content)	chunkqueueFree(response->content);

	//Обнуление структуры response_s
	memset(response, '\0', sizeof(response_s));

	return response;
}//END: responseClear



/*
 * Функция возвращает текстовое описание HTTP версии используемого протокола
 */
const char *
responseHTTPVersionString(http_version_e v){
	switch(v){
		case HTTP_VERSION_1_0: return "HTTP/1.0";
		case HTTP_VERSION_1_1:
		default: return "HTTP/1.1";
	}
}//END: responseHTTPVersionString


/*
 * Функция возвращает текстовое представление кода HTTP ответа, согласно номеру ответа
 */
const char *
responseCodeCode(int http_code){
	switch (http_code) {
		case 100: return "100";	case 101: return "101";
		case 200: return "200";	case 201: return "201";
		case 202: return "202";	case 203: return "203";
		case 204: return "204";	case 205: return "205";
		case 206: return "206";
		case 300: return "300";	case 301: return "301";
		case 302: return "302";	case 303: return "303";
		case 304: return "304";	case 305: return "305";
		case 307: return "307";
		case 400: return "400";	case 401: return "401";
		case 402: return "402";	case 403: return "403";
		case 404: return "404";	case 405: return "405";
		case 406: return "406";	case 407: return "407";
		case 408: return "408";	case 409: return "409";
		case 410: return "410";	case 411: return "411";
		case 412: return "412";	case 413: return "413";
		case 414: return "414";	case 415: return "415";
		case 416: return "416";	case 417: return "417";
		case 429: return "429";
		case 500: return "500";	case 501: return "501";
		case 502: return "502";	case 503: return "503";
		case 504: return "504";	case 505: return "505";
		default:  return "000";
	}
}


/*
 * Функция возвращает текстовое описание кода HTTP ответа, согласно номеру ответа
 */
const char *
responseCodeString(int http_code){

	switch (http_code) {

		case 100: return "Continue";	//сервер удовлетворён начальными сведениями о запросе, клиент может продолжать пересылать заголовки. Появился в HTTP/1.1
		case 101: return "Switching Protocols";

		case 200: return "OK";
		case 201: return "Created";
		case 202: return "Accepted";
		case 203: return "Non-Authoritative Information";
		case 204: return "No Content";
		case 205: return "Reset Content";
		case 206: return "Partial Content";

		case 300: return "Multiple Choices";
		case 301: return "Moved Permanently";	//запрошенный документ был окончательно перенесен на новый URI, указанный в поле Location заголовка
		case 302: return "Moved Temporarily";	//запрошенный документ временно доступен по другому URI, указанному в заголовке в поле Location
		case 303: return "See Other";	//документ по запрошенному URI нужно запросить по адресу в поле Location заголовка с использованием метода GET несмотря даже на то, что первый запрашивался иным методом
		case 304: return "Not Modified";
		case 305: return "Use Proxy";
		case 307: return "Temporary Redirect";	//запрашиваемый ресурс на короткое время доступен по другому URI, указанный в поле Location заголовка

		case 400: return "Bad Request";	//сервер обнаружил в запросе клиента синтаксическую ошибку
		case 401: return "Unauthorized";	//для доступа к запрашиваемому ресурсу требуется аутентификация
		case 402: return "Payment Required";
		case 403: return "Forbidden";	//сервер понял запрос, но он отказывается его выполнять из-за ограничений в доступе для клиента к указанному ресурсу
		case 404: return "Not Found";	//Сервер понял запрос, но не нашёл соответствующего ресурса по указанному URI
		case 405: return "Method Not Allowed";	//указанный клиентом метод нельзя применить к текущему ресурсу
		case 406: return "Not Acceptable";
		case 407: return "Proxy Authentication Required";
		case 408: return "Request Timeout";	//время ожидания сервером передачи от клиента истекло. Клиент может повторить аналогичный предыдущему запрос в любое время
		case 409: return "Conflict";
		case 410: return "Gone";
		case 411: return "Length Required";	//для указанного ресурса клиент должен указать Content-Length в заголовке запроса. Без указания этого поля не стоит делать повторную попытку запроса к серверу по данному URI
		case 412: return "Precondition Failed";
		case 413: return "Request Entity Too Large";	//возвращается в случае, если сервер отказывается обработать запрос по причине слишком большого размера тела запроса
		case 414: return "Request-URI Too Long";	//сервер не может обработать запрос из-за слишком длинного указанного URL
		case 415: return "Unsupported Media Type";	//по каким-то причинам сервер отказывается работать с указанным типом данных при данном методе
		case 416: return "Requested Range Not Satisfiable";	//запрашиваемый диапазон не достижим. в поле Range заголовка запроса был указан диапазон за пределами ресурса и отсутствует поле If-Range
		case 417: return "Expectation Failed";	//по каким-то причинам сервер не может удовлетворить значению поля Expect заголовка запроса
		case 429: return "Too Many Requests";	//клиент попытался отправить слишком много запросов за короткое время, что может указывать, например, на попытку DoS-атаки. Может сопровождаться заголовком Retry-After, указывающим, через какое время можно повторить запрос

		case 500: return "Internal Server Error";	//любая внутренняя ошибка сервера, которая не входит в рамки остальных ошибок класса
		case 501: return "Not Implemented";	//сервер не поддерживает возможностей, необходимых для обработки запроса
		case 502: return "Bad Gateway";	//сервер, выступая в роли шлюза или прокси-сервера, получил недействительное ответное сообщение от вышестоящего сервера
		case 503: return "Service Unavailable";	//сервер временно не имеет возможности обрабатывать запросы по техническим причинам (обслуживание, перегрузка и прочее)
		case 504: return "Gateway Timeout";	//сервер в роли шлюза или прокси-сервера не дождался ответа от вышестоящего сервера для завершения текущего запроса. Появился в HTTP/1.1
		case 505: return "HTTP Version Not Supported";	// сервер не поддерживает или отказывается поддерживать указанную в запросе версию протокола HTTP. Появился в HTTP/1.1
		default:  return "Undefined HTTP error";
	}
	return "Undefined"; //Неизвестная ошибка
}//END: responseCodeString



/*
 * Перенаправление
 */
void
responseHttpLocation(connection_s * con, const char * location, bool temporarily){

	con->http_code = (temporarily ? 302 : 301);

	//Переходим на начало буфера
	bufferSeekBegin(con->response.head);

	//Headers
	bufferAddStringFormat(
		con->response.head, 
		"%s %s %s\r\n" \
		"Server: %s\r\n" \
		"Location: %s\r\n" \
		"Connection: close\r\n" \
		"\r\n",
		responseHTTPVersionString(con->request.http_version),
		responseCodeCode(con->http_code),
		responseCodeString(con->http_code),
		XG_SERVER_VERSION,
		location
	);

	connectionSetStage(con, CON_STAGE_WORKING);

/*
	bufferPrint(con->response.head);
*/
}//END: responseHttpLocation




/*
 * Подготовка ответа ошибки сервера
 */
void
responseHttpError(connection_s * con){

	//Переходим на начало буфера
	bufferSeekBegin(con->response.head);

	//При 304 (файл не изменен) никакого контента не возвращаем
	if(con->http_code != 304){
		buffer_s * body = bufferCreate(256);
		//Body
		//Если был AJAX запрос
		if(con->request.is_ajax == true){
			bufferAddStringFormat(
				body, 
				"{status:\"error\",\"einfo\":{\"type\":\"http\",\"code\":%s, \"desc\":\"%s\"}}",
				responseCodeCode(con->http_code),
				responseCodeString(con->http_code)
			);
		}else{
			bufferAddStringFormat(
				body, 
				"<html><head><title>%s: %s</title></head><body><h1>%s: %s</h1></body></html>",
				responseCodeCode(con->http_code),
				responseCodeString(con->http_code),
				responseCodeCode(con->http_code),
				responseCodeString(con->http_code)
			);
		}

		chunkqueueAddBuffer(con->response.content, body, 0, body->count, true);
	}

	//Headers
	bufferAddStringFormat(
		con->response.head, 
		"%s %s %s\r\n" \
		"Server: %s\r\n" \
		"Content-Type: text/html; charset=UTF-8\r\n" \
		"Content-Length: %d\r\n" \
		"Date: %g\r\n" \
		"Connection: close\r\n" \
		"\r\n",
		responseHTTPVersionString(con->request.http_version),
		responseCodeCode(con->http_code),
		responseCodeString(con->http_code),
		XG_SERVER_VERSION,
		(int64_t)con->response.content->content_length,
		(time_t)time(NULL)
	);

	connectionSetStage(con, CON_STAGE_WORKING);
/*
	bufferPrint(con->response.head);
*/
}//END: responseHttpError



/*
 * Добавляет Cookie в ответ сервера
 * Set-Cookie: KEY="%3Citems%3E%3C%2Fitems%3E";Path=/
 */
bool
responseSetCookie(response_s * response, const char * key_name, const char * value){

	if(!key_name || !value) return false;
	if(!response->cookie) response->cookie = kvNewRoot();

	uint32_t key_len = strlen(key_name);

	//Создание буффера для составления Cookie значения
	buffer_s * buf = encodeUrlQuery(value, 0, NULL);
	kv_s * node = kvAppend(response->cookie, key_name, key_len, KV_REPLACE);
	if(buf){
		kvSetString(node, buf->buffer, buf->count);
		bufferFree(buf);
	}else{
		kvSetString(node, NULL, 0);
	}

	return true;
}//END: responseSetCookie



/*
 * Добавляет заголовок в ответ сервера
 */
bool
responseSetHeader(response_s * response, const char * header, const char * value, kv_rewrite_rule rewrite){

	if(!header || !value) return false;
	if(!response->headers) response->headers= kvNewRoot();
	kvAppendString(response->headers, header, value, 0, rewrite);

	return true;
}//END: responseSetHeader



/*
 * Подготавливает первую строку ответа сервера
 */
void
responseBuildFirstLine(buffer_s * buffer, int http_code, http_version_e http_version){
	if(buffer->index > 0) bufferSeekBegin(buffer);
	bufferAddStringFormat(
		buffer, 
		"%s %s %s\r\n",
		responseHTTPVersionString(http_version),
		responseCodeCode(http_code),
		responseCodeString(http_code)
	);
}//END: responseBuildFirstLine



/*
 * Генерация заголовков ответа сервера
 */
void
responseBuildHeaderLines(buffer_s * buffer, kv_s * headers){
	if(!headers) return;
	char tmp[256];
	struct tm tm;
	time_t ts = time(NULL);
	localtime_r(&ts, &tm);
	uint32_t tmp_len = strftime(tmp, sizeof(tmp)-1, XG_DATETIME_GMT_FORMAT, &tm);

	kvAppendString(headers, "Server", XG_SERVER_VERSION, 0, KV_BREAK);
	kvAppendString(headers, "Date", tmp, tmp_len, KV_BREAK);
	kvAppendString(headers, "Content-Type", "text/html; charset=UTF-8", 24, KV_BREAK);
	kvAppendString(headers, "Connection", "close", 5, KV_REPLACE);

	//Заголовки ответа сервера
	kvEcho(headers, KVF_HEADERS, buffer);

}//END: responseBuildHeaderLines



/*
 * Генерация заголовков Cookie в ответ сервера
 * Set-Cookie: KEY=%3Citems%3E%3C%2Fitems%3E;Path=/
 */
void
responseBuildCookieLines(buffer_s * buffer, kv_s * cookie){
	if(!cookie) return;
	kv_s * node;
	for(node = cookie->value.v_list.first; node; node = node->next){
		bufferAddStringFormat(
			buffer, 
			"Set-Cookie: %s=%s\r\n",
			node->key_name,
			node->value.v_string.ptr
		);
	}
}//END: responseBuildCookieLines




/*
 * Генерация заголовка ответа сервера
 */
void
responseBuildHeaders(connection_s * con){

	//Переходим на начало буфера
	bufferSeekBegin(con->response.head);

	//Генерация первой строки ответа сервера
	responseBuildFirstLine(con->response.head, con->http_code, con->request.http_version);

	//Генерация заголовков ответа сервера
	responseBuildHeaderLines(con->response.head, con->response.headers);

	//Генерация заголовков Cookie
	responseBuildCookieLines(con->response.head, con->response.cookie);

	//Завершающая пустая строка
	bufferAddStringN(con->response.head, "\r\n", 2);

}//END: responseBuildHeaders



/*
 * Подготовка к отправке статичного файла клиенту
 */
result_e
responseStaticFile(connection_s * con, static_file_s * sf){

	result_e result = RESULT_OK;

	request_range_s * range = con->request.ranges;
	buffer_s * body;
	kv_s * node;
	size_t range_index = 0;
	int64_t size_n = (int64_t) sf->st.st_size;
	char * size_s = intToString(size_n, NULL);
	int64_t total = 0;
	bool multipart = false;
	char * tmp;


	/*
	HTTP/1.1 206 Partial content
	Date: Wed, 15 Nov 1995 06:25:24 GMT
	Last-modified: Wed, 15 Nov 1995 04:58:08 GMT
	Content-type: multipart/byteranges; boundary=THIS_STRING_SEPARATES

	--THIS_STRING_SEPARATES
	Content-type: application/pdf
	Content-range: bytes 500-999/8000

	...the first range...
	--THIS_STRING_SEPARATES
	Content-type: application/pdf
	Content-range: bytes 7000-7999/8000

	...the second range
	--THIS_STRING_SEPARATES--
	*/

	//Запрошена часть файла
	if(range != NULL){

		con->http_code = 206;	//206 Partial content

		//Проверка всех диапазонов Range
		for(;range; range = range->next){
			if(range->begin_n < 0 || range->begin_n >= size_n || range->length == 0 || range->length > size_n) goto label_error_416;
			switch(range->seek){
				case R_SEEK_START:
					if(range->length < 0){
						range->length = size_n - range->begin_n;
					}else{
						if(range->begin_n + range->length > size_n) goto label_error_416;
					}
				break;
				case R_SEEK_END:
					range->begin_n = size_n - range->length;
				break;
				default: 
					goto label_error_500;
			}
			total += range->length;	//Общий объем запрошеных частей
		}//Проверка всех диапазонов Range

		range = con->request.ranges;
		multipart = (range->next == NULL ? false : true);

		//Если совокупный размер запрошенных диапазонов больше размера файла
		//то отправляем файл целиком (это меньше данных) одним куском 
		if(total > size_n){
			range->begin_n	= 0;
			range->length	= size_n;
			multipart		= false;
		}


		//Запрошено несколько частей файла
		if(multipart){

			node = kvAppendString(con->response.headers, "Content-Type", "multipart/byteranges; boundary=123456789_123456789_123456", 57, KV_REPLACE);
			char * boundary = &node->value.v_string.ptr[31];
			stringRandom(26, boundary);

			//Запрошенные части контента
			for(;range;range = range->next){

				body = bufferCreate(192);

				bufferAddStringFormat(
					body,
					"%s--%s\r\n"	\
					"Content-type: %s\r\n"	\
					"Content-range: bytes %d-%d/%s\r\n"	\
					"\r\n",
					(range_index > 0 ? "\r\n" : ""),
					boundary,
					sf->mimetype.ptr,
					(int64_t)range->begin_n,
					(int64_t)(range->begin_n+range->length-1),
					size_s
				);

				chunkqueueAddBuffer(con->response.content, body, 0, body->count, true);

				//Чтение нужного блока из файла в буфер
				chunkqueueAddFile(con->response.content, sf, range->begin_n, range->length);

				range_index++;
			}//Запрошенные части контента

			//Завершающая граница --BOUNDARY--
			tmp = mNew(34);
			chunkqueueAddHeap(con->response.content, tmp, 0, snprintf(tmp, 33,"\r\n--%s--",boundary), true);

		}
		//Запрошена одна часть файла
		else{

			kvAppendString(con->response.headers, "Content-Type", sf->mimetype.ptr, sf->mimetype.len, KV_REPLACE);
			//Content-Range: bytes 64312833-64657026/64657027
			buffer_s * tmp_buf = bufferCreate(128);
			bufferAddStringFormat(
				tmp_buf, 
				"bytes %d-%d/%s", 
				(int64_t)range->begin_n, 
				(int64_t)(range->begin_n + range->length-1), 
				size_s
			);
			node = kvAppendString(con->response.headers, "Content-Range", tmp_buf->buffer, tmp_buf->count, KV_REPLACE);
			bufferFree(tmp_buf);

			//Чтение нужного блока из файла в буфер
			chunkqueueAddFile(con->response.content, sf, range->begin_n, range->length);
		}
	}
	//Запрошен файл целиком
	else{
		kvAppendString(con->response.headers, "Content-Type", sf->mimetype.ptr, sf->mimetype.len, KV_REPLACE);
		//Чтение файла в буфер
		chunkqueueAddFile(con->response.content, sf, 0, (uint32_t)size_n);
		kvAppendString(con->response.headers, "ETag", sf->etag->ptr, sf->etag->len, KV_REPLACE);
	}

	//Заголовки ответа
	kvAppendInt(con->response.headers, "Content-Length", con->response.content->content_length, KV_REPLACE);
	kvAppendString(con->response.headers, "Accept-Ranges", "bytes", 5, KV_REPLACE);

	char time_tmp[256];
	struct tm tm;
	localtime_r(&sf->st.st_mtime, &tm);
	uint32_t tmp_len = strftime(time_tmp, sizeof(time_tmp)-1, XG_DATETIME_GMT_FORMAT, &tm);
	kvAppendString(con->response.headers, "Last-Modified", time_tmp, tmp_len, KV_REPLACE);

	//Подготовка заголовков ответа
	responseBuildHeaders(con);
/*
	bufferPrint(con->response.head);
*/

	goto label_end;

	//500
	label_error_500:
	con->http_code = 500;
	result = RESULT_ERROR;
	goto label_end;

	//416
	label_error_416:
	con->http_code = 416;
	result = RESULT_ERROR;

	//end
	label_end:
	mFree(size_s);
	return result;
}//END: responseSendStaticFile


