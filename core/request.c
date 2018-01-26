/***********************************************************************
 * XG SERVER
 * core/request.c
 * Функции работы с клиентским запросом request_s
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/   


#include "server.h"

/***********************************************************************
 * Работа с запросом request_s
 **********************************************************************/ 


/*
 * Освобождение структуры request_range_s
 */
static void
requestHttpRangesFree(request_range_s * ranges){
	if(!ranges) return;
	request_range_s * r_current = ranges;
	request_range_s * r_free;
	while(r_current){
		r_free = r_current;
		r_current = r_current->next;
		mFree(r_free);
	}
}//END: requestHttpRangesFree




/*
 * Освобождение структуры request_s
 */
inline void
requestFree(request_s * request){
	mFree(requestClear(request));
}//END: requestFree



/*
 * Очистка структуры request_s
 */
request_s * 
requestClear(request_s * request){

	//Освобождение занятой памяти
	if(request->data)		bufferFree(request->data);
	if(request->headers)	kvFree(request->headers);
	if(request->get)		kvFree(request->get);
	if(request->post)		kvFree(request->post);
	if(request->cookie)		kvFree(request->cookie);
	if(request->files)		kvFree(request->files);
	if(request->ranges)		requestHttpRangesFree(request->ranges);
	if(request->static_file) requestStaticFileFree(request->static_file);
	mStringClear(&(request->host));
	mStringClear(&(request->multipart_boundary));
	mStringClear(&(request->uri.uri));
	mStringClear(&(request->uri.path));
	mStringClear(&(request->uri.query));
	mStringClear(&(request->uri.fragment));

	//Обнуление структуры request_s
	memset(request, '\0', sizeof(request_s));

	return request;
}//END: requestClear



/*
 * Парсинг URI адреса запроса в структуру request_uri_s
 */
result_e
requestParseURI(request_uri_s * uri, const char * raw_uri, size_t ilen, const_string_s * directory_index){
	if(!uri || !raw_uri || !ilen || raw_uri[0] != '/') return RESULT_ERROR;


	//Копирование URI в структуру request_uri_s
	uri->uri.ptr = stringCloneN(raw_uri, ilen, &(uri->uri.len)); 

	register const char * curstr = uri->uri.ptr;
	register const char * tmpstr = curstr;
	int len;
	register char * ptr;

	//Получение пути
	while ( *tmpstr != '\0' && *tmpstr != '#'  && *tmpstr != '?'){
		//Если URI содержит недопустимые символы
		//Если URI содержит .. или ./ или /. или // - ошибка
		if(*tmpstr < 32 || *tmpstr >= 127 || *(uint16_t *)tmpstr == 0x2E2E || *(uint16_t *)tmpstr == 0x2F2E || *(uint16_t *)tmpstr == 0x2E2F || *(uint16_t *)tmpstr == 0x2F2F) return RESULT_ERROR;
		tmpstr++;
	}
	len = tmpstr - curstr;
	if(len > 0){

		if(len > request_path_max) return RESULT_ERROR;	//Превышена максимальная длинна маршрута в запросе

		//Если последний символ URI пути = "/" -> запрошена директория, подставляем значение "/webserver/directory_index"
		if(*(tmpstr-1) == '/'){
			uri->path.len = len + directory_index->len;
			ptr = uri->path.ptr = (char *)mNew(uri->path.len + 1);
			ptr += stringCopyN(ptr, curstr, len);
			ptr += stringCopyN(ptr, directory_index->ptr, directory_index->len);
		}else{
			uri->path.ptr = stringCloneN(curstr, len, &(uri->path.len)); 
		}
	}else{
		uri->path.len = directory_index->len + 1;
		ptr = uri->path.ptr = (char *)mNew(uri->path.len + 1);
		ptr += stringCopyN(ptr, "/", 1);
		ptr += stringCopyN(ptr, directory_index->ptr, directory_index->len);
	}
	curstr = tmpstr;

	//printf("uri->path.ptr = [%s]\n",uri->path.ptr);

	//Если задан query
	if(*curstr == '?') {
		//Пропускаем "?"
		curstr++;
		//Чтение query
		tmpstr = curstr;
		while (*tmpstr!='\0' && *tmpstr!='#') tmpstr++;
		len = tmpstr - curstr;
		if(len > 0) uri->query.ptr = stringCloneN(curstr, len, &(uri->query.len));
		curstr = tmpstr;
	}

	//Если задан fragment
	if (*curstr == '#'){
		//Пропускаем "#"
		curstr++;
		//Чтение fragment
		tmpstr = curstr;
		while(*tmpstr != '\0') tmpstr++;

		len = tmpstr - curstr;
		if(len > 0) uri->fragment.ptr = stringCloneN(curstr, len, &(uri->fragment.len));
		curstr = tmpstr;
	}
	
	return RESULT_OK;
}//END: requestParseURI




/*
 * Парсинг первой строки заголовков запроса GET /uri HTTP/x.y[\r\n],
 * возвращает 0 в случае успеха или код HTTP ошибки
 */
int
requestParseFirstLine(connection_s * con, const char * line, size_t len){

	request_s * request = &con->request;
	register const char * ptr = line;
	register const char * tmp;
	register const char * end = line + len;
	size_t n = 0;

	//Метод запроса: GET / POST
	if(*(int32_t *)ptr == 0x20544547){
		request->request_method = HTTP_GET;
		n = 4;
	}
	else if(*(int32_t *)ptr == 0x54534f50 && ptr[4] == ' '){
		request->request_method = HTTP_POST;
		n = 5;
	}
	else{
		RETURN_ERROR(501, "501 Not Implemented");	//501 Not Implemented: сервер не понимает указанный в запросе метод
	}

	//Пропускаем GET / POST
	ptr += n;

	//Проверка начала URI (GET /uri... HTTP/x.y)
	if(*ptr!='/') RETURN_ERROR(400, "400 Bad Request");	//400 Bad Request: некорректный запрос -> символ начала URI [/] не найден

	//Определение URI и его длинны
	tmp = ptr; 
	while(tmp < end && *tmp != '\0' && !isspace((int)*tmp)) tmp++;

	//Если при поиске URI не найден пробел -> ошибка
	//GET [/uri...] HTTP/x.y
	if(*tmp!=0x20){
		RETURN_ERROR(400, "400 Bad Request"); //400 Bad Request: некорректный запрос -> пробел после URI не найден
	}

	//Парсинг URI
	if(requestParseURI(&(request->uri), ptr, tmp - ptr, (const_string_s *)&con->server->config.directory_index) != RESULT_OK) RETURN_ERROR(400, "400 Bad Request"); //400 Bad Request: некорректный запрос -> ошибка парсинга URI запроса

	//Пропускаем пробел
	tmp++;
	ptr = tmp;

	//Проверка наличия "HTTP/"
	if (*(int32_t *)ptr != 0x50545448) RETURN_ERROR(400, "400"); //400 Bad Request: некорректный запрос -> не найдено HTTP
	//Пропускаем HTTP
	ptr+=4;
	switch(*(int32_t *)ptr){
		case 0x302E312F: request->http_version = HTTP_VERSION_1_0; break;	// HTTP/1.0
		case 0x312E312F: request->http_version = HTTP_VERSION_1_1; break;	// HTTP/1.1
		default: RETURN_ERROR(505, "505"); //505 HTTP Version Not Supported:  сервер не поддерживает или отказывается поддерживать указанную в запросе версию протокола HTTP
	}

	return 0;
}//END: requestParseFirstLine




/*
 * Функция обрабатывает строку заголовка запроса, возвращает 0 в случае успеха или код HTTP ошибки
 */
int
requestParseHeaderLine(connection_s * con, const char * line, uint32_t len){

	request_s * request = &con->request;
	register const char * ptr = line;
	register const char * tmp;
	register const char * end = line + len;
	size_t n = 0;
	kv_s * node;

	//Получение заголовков запроса
	//Каждая строка заголовка имеет следующий вид:
	//[ключ]: [значение]\r\n

	//Поиск разделителя :
	tmp = charSearchN(ptr, ':', len);
	if(tmp == NULL) RETURN_ERROR(400, "400"); //400 Bad Request: не найден разделитель [:] или найден за пределами строки

	n = tmp - ptr;
	if(!n) RETURN_ERROR(400, "400"); //400 Bad Request: найден ключ нулевой длинны

	//Создание ключа
	if(!request->headers) request->headers = kvNewRoot();
	node = kvAppend(request->headers, ptr, n, KV_REPLACE);

	//Пропускаем [:]
	tmp++;

	//Пропускаем пробелы после [:]
	while(*tmp==0x20)tmp++;

	ptr = tmp;
	n = end - ptr;

	//Присвоение значения ключу
	if(n > 0) kvSetString(node, ptr, n);

	return 0;
}//END: requestParseHeaderLine



/*
 * Обработка заголовков запроса в переменные соединения, возвращает 0 в случае успеха или код HTTP ошибки
 */
int
requestHeadersToVariables(connection_s * con){
	request_s * request = &(con->request);
	kv_s * headers = request->headers;
	if(!headers) return 0;
	kv_s * node;
	int n;
	const char * ptr;

	//Просмотр заголовков
	for(node = headers->value.v_list.first; node; node = node->next){
		if(!node->key_name || !node->key_len) continue;

		//Найден Content-Length
		if(BIT_ISUNSET(request->headers_bits,HEADER_CONTENT_LENGTH) && node->key_len == 14 && stringCompareCaseN(node->key_name,"Content-Length", 14)){
			request->headers_bits |= HEADER_CONTENT_LENGTH;
			con->request.content_length = atol(node->value.v_string.ptr);
			//Метод запроса - не POST
			if(con->request.request_method != HTTP_POST && con->request.content_length > 0){
				RETURN_ERROR(400,"Warning: Content-Length is forbidden for GET request method");
				//400 Bad request
			}
			//Значение Content-Length больше лимита на размер POST контента
			if(con->request.content_length > con->server->config.max_post_size){
				RETURN_ERROR(413, "Warning: Content-Length too large [%u] but maximum is [%u]", con->request.content_length, con->server->config.max_post_size);
				//413 Request Entity Too Large: Очень длинный запрос
			}
			continue;
		}

		//Найден Content-Type
		if(BIT_ISUNSET(request->headers_bits,HEADER_CONTENT_TYPE) && node->key_len == 12 && stringCompareCaseN(node->key_name,"Content-Type", 12)){
			request->headers_bits |= HEADER_CONTENT_TYPE;
			//multipart/form-data; boundary=----WebKitFormBoundaryZd4wrriBn2H7dq1A
			if(stringCompareCaseN(node->value.v_string.ptr, "multipart/form-data;", 20)){
				con->request.post_method = POST_MULTIPART;
				n = 0;
				if(stringCompareCaseN(node->value.v_string.ptr+21, "boundary=", 9)){
					ptr = node->value.v_string.ptr+30;
					if(*ptr != '\0'){
						con->request.multipart_boundary.ptr = stringClone(ptr, &(con->request.multipart_boundary.len));
						DEBUG_MSG("MULTIPART BOUNDARY FOUND = [%s]\n", con->request.multipart_boundary.ptr);
						n = 1;
					}
				}
				if(!n){
					RETURN_ERROR(400, "Warning: bad headers -> content type is [multipart/form-data] but boundary not set.");
					//400 Bad Request: найден ключ нулевой длинны
				}
			}
			else 
			if(stringCompareCaseN(node->value.v_string.ptr, "application/x-www-form-urlencoded", 33)){
				con->request.post_method = POST_URLENCODED;
			}
			else{
				RETURN_ERROR(400,"Warning: bad headers -> content type for POST request is undefined [%s]\n", node->value.v_string.ptr);
				//400 Bad Request: найден ключ нулевой длинны
			}
			continue;
		}

		//Найден Cookie
		if(BIT_ISUNSET(request->headers_bits,HEADER_COOKIE) && node->key_len == 6 && stringCompareCaseN(node->key_name,"Cookie", 6)){
			request->headers_bits |= HEADER_COOKIE;
			con->request.cookie = requestParseCookies(node->value.v_string.ptr);
			continue;
		}

		//Найден Range
		if(BIT_ISUNSET(request->headers_bits,HEADER_RANGE) && node->key_len == 5 && stringCompareCaseN(node->key_name,"Range", 6)){
			request->headers_bits |= HEADER_RANGE;
			if(stringCompareCaseN(node->value.v_string.ptr, "bytes=", 6)){
				//Разбираем HTTP Ranges
				con->request.ranges = requestParseHttpRanges(node->value.v_string.ptr+6, &n);
				//Если в процессе разбора возникла ошибка - возвращаем ее (ошибка имеет номер HTTP ошибки)
				if(n != 0) RETURN_ERROR(n,"HTTP Ranges parsing error");
			}
			continue;
		}

		//Найден X-Requested-With
		if(BIT_ISUNSET(request->headers_bits,HEADER_X_REQUESTED_WITH) && node->key_len == 16 && stringCompareCaseN(node->key_name,"X-Requested-With", 16)){
			request->headers_bits |= HEADER_X_REQUESTED_WITH;
			if(stringCompareCaseN(node->value.v_string.ptr, "XMLHttpRequest", 14)) con->request.is_ajax = true;
			continue;
		}

		//Найден Host
		if(BIT_ISUNSET(request->headers_bits,HEADER_HOST) && node->key_len == 4 && stringCompareCaseN(node->key_name,"Host", 4)){
			if(node->value.v_string.len > 0){
				request->headers_bits |= HEADER_HOST;
				//host:port
				if((ptr = strchr(node->value.v_string.ptr, ':')) != NULL){
					con->request.host.ptr = stringCloneN(node->value.v_string.ptr, ptr - node->value.v_string.ptr, &(con->request.host.len));
				}else{
					con->request.host.ptr = stringCloneN(node->value.v_string.ptr, node->value.v_string.len, &(con->request.host.len));
				}
			}
			continue;
		}

		//Найден If-None-Match
		if(BIT_ISUNSET(request->headers_bits,HEADER_IF_NONE_MATCH) && node->key_len == 13 && stringCompareCaseN(node->key_name,"If-None-Match", 13)){
			request->headers_bits |= HEADER_IF_NONE_MATCH;
			request->if_none_match.ptr = (const char *)node->value.v_string.ptr;
			request->if_none_match.len = node->value.v_string.len;
			continue;
		}

		//Найден User-Agent
		if(BIT_ISUNSET(request->headers_bits,HEADER_USER_AGENT) && node->key_len == 10 && stringCompareCaseN(node->key_name,"User-Agent", 10)){
			request->headers_bits |= HEADER_USER_AGENT;
			request->user_agent.ptr = (const char *)node->value.v_string.ptr;
			request->user_agent.len = node->value.v_string.len;
			continue;
		}

		//Найден Referer
		if(BIT_ISUNSET(request->headers_bits,HEADER_REFERER) && node->key_len == 7 && stringCompareCaseN(node->key_name,"Referer", 7)){
			request->headers_bits |= HEADER_REFERER;
			request->referer.ptr = (const char *)node->value.v_string.ptr;
			request->referer.len = node->value.v_string.len;
			continue;
		}

	}//Просмотр заголовков


	//con->request.ranges = requestParseHttpRanges("0-9,10-19,20-29", &n);
	//con->request.ranges = requestParseHttpRanges("10-20", &n);
	//if(n != 0) printf("HTTP Ranges parsing error: %d\n",n);


	//Обработка полученных результатов для выявления ошибок

	if(BIT_ISUNSET(request->headers_bits,HEADER_HOST)) RETURN_ERROR(400,"400");	//Bad request - не задан Host

	//POST запрос
	if(con->request.request_method == HTTP_POST){
		if(BIT_ISUNSET(request->headers_bits,HEADER_CONTENT_LENGTH)) RETURN_ERROR(411,"411");	//Length Required - для указанного ресурса клиент должен указать Content-Length в заголовке запроса
		if(con->request.post_method == POST_UNDEFINED) RETURN_ERROR(400,"400");	//Метод POST запроса не опредлен
	}

	return 0;
}//END: requestHeadersToVariables




/*
 * Парсинг Cookie в структуру KV
 */
kv_s * 
requestParseCookies(const char * cookies){
	if (!cookies) return NULL;
	kv_s * node;
	kv_s * root = kvNewRoot();
	register const char * ptr = cookies;
	register const char * tmp;
	register uint32_t len;
	buffer_s * buf = bufferCreate(2048);

	while(*ptr){

		while(*ptr && isspace((int)*ptr))ptr++;
		if(*ptr == '\0') break;

		tmp = strchr(ptr, '=');
		if(tmp == NULL) break;
		len = tmp - ptr;
		if(len > 0) node = kvAppend(root, ptr, len, KV_REPLACE);

		//Пропускаем [=]
		tmp++;
		ptr = tmp;
		while(*tmp && *tmp != '\r' && *tmp!='\n' && *tmp!=';') ++tmp;
		bufferSeekBegin(buf);
		buf = decodeUrlQuery(ptr, tmp - ptr, buf);
		kvSetString(node, buf->buffer, buf->count);
		if(*tmp != ';') break;
		tmp++;
		ptr = tmp;
	}

	bufferFree(buf);
	return root;
}//END: requestParseCookies




/*
 * Парсинг HTTP Range
 * - The first 500 bytes (byte offsets 0-499, inclusive):  bytes=0-499
 * - The second 500 bytes (byte offsets 500-999, inclusive): bytes=500-999
 * - The final 500 bytes (byte offsets 9500-9999, inclusive): bytes=-500 или bytes=9500-
 * - The first and last bytes only (bytes 0 and 9999):  bytes=0-0,-1
 * - Several legal but not canonical specifications of the second 500 bytes (byte offsets 500-999, inclusive):
 * 		bytes=500-600,601-999
 * 		bytes=500-700,601-999
 */
request_range_s * 
requestParseHttpRanges(const char * ptr, int * error){

	*error = 0;
	request_range_s *	first = NULL;	//Первый блок, описывающий Http Ranges
	request_range_s *	current;		//Текущий блок
	seek_position_e		seek;			//Откуда смещение (R_SEEK_START - от начала фала, R_SEEK_END - от конца файла)
	int64_t				begin_n;		//Начало блока (-1 = от начала/конца файла)
	int64_t				end_n;			//Начало блока (-1 = от начала/конца файла)
	int64_t				length;			//Количество байт для отправки
	size_t				chunks = 0;		//Количество "кусков"

	while(*ptr){

		//Позиция от начала файла
		if(*ptr>='0' && *ptr<='9'){
			seek = R_SEEK_START;
			begin_n = stringToInt64(ptr, &ptr);	//Позиция ОТ
			if(*ptr != '-'){*error=416; RETURN_ERROR(first,"*ptr != '-' -> [%s]",ptr);}
			ptr++; //пропускаем "-"
			//от begin_n до конца файла
			if(*ptr==','||!*ptr){
				length = -1;
			}
			else{
				if(*ptr<'0' || *ptr>'9'){*error=416; RETURN_ERROR(first,"*ptr <> 0..9");}
				end_n = stringToInt64(ptr, &ptr);	//Позиция ДО
				if(end_n < begin_n){*error=416; RETURN_ERROR(first,"end_n < begin_n");}	//Конечная позиция не может быть меньше начальной
				length = end_n - begin_n + 1;	//Количество байт, которые требуется прочитать
			}
		}else
		//Позиция от конца файла
		if(*ptr == '-'){
			ptr++; //пропускаем "-"
			if(*ptr<'0' || *ptr>'9'){*error=416; RETURN_ERROR(first,"*ptr <> 0..9");}
			seek = R_SEEK_END;
			begin_n = 0;
			length = stringToInt64(ptr, &ptr);	//Позиция ДО
			if(!length){*error=416; RETURN_ERROR(first,"!length");}	//Нельзя прочитать 0 последних байт
		}
		//Ошибка
		else{
			*error = 416; RETURN_ERROR(first,"Undefined symbol: [%s]",ptr);
		}

		if(!first){
			first = (request_range_s *)mNewZ(sizeof(request_range_s));
			current = first;
		}else{
			current->next = (request_range_s *)mNewZ(sizeof(request_range_s));
			current = current->next;
			//Ограничение на количество диапазонов
			if(chunks++ > 10){
				*error = 416; RETURN_ERROR(first,"Range chunks more than 10 -> exploit?");
			}
		}
		current->seek		= seek;
		current->begin_n	= begin_n;
		current->length		= length;

		ptr=strchr(ptr,',');
		if(!ptr) break;
		ptr++;
	}

	return first;

}//END: requestParseHttpRanges



/*
 * Вывод на экран структуры request_range_s
 */
void
requestHttpRangesPrint(request_range_s * ranges){
	if(!ranges){
		printf("HTTP Ranges is empty\n");
		return;
	}
	request_range_s * r_current = ranges;
	int n = 0;
	while(r_current){
		printf("%d: (%d) pos: %d -> len: %d\n", n, (int)r_current->seek, (int)r_current->begin_n, (int)r_current->length);
		r_current = r_current->next; n++;
	}
}//END: requestHttpRangesFree



/*
 * Функция обрабатывает POST запрос application/x-www-form-urlencoded
 */
result_e
requestParseUrlEncodedForm(connection_s * con){
	buffer_s * buf = con->request.data;
	request_parser_s * parser = &(con->request.parser);
	con->request.post = kvFromQueryString(&buf->buffer[parser->body_n]);
	return RESULT_OK;
}//END: requestParseUrlEncodedForm




/*
 * Функция обрабатывает POST запрос multipart/form-data
 */
/*
------WebKitFormBoundaryrCtjrJ8JevAUKXAd
Content-Disposition: form-data; name="str1"

str1 string
------WebKitFormBoundaryrCtjrJ8JevAUKXAd
Content-Disposition: form-data; name="str2"

str2 string
------WebKitFormBoundaryrCtjrJ8JevAUKXAd
Content-Disposition: form-data; name="file1"; filename="dh1024.pem"
Content-Type: application/x-x509-ca-cert

-----BEGIN DH PARAMETERS-----
MIGHAoGBANmAnfkETuKHOCWaE+W+F3kM/e7z5A8hZb7OqwGMQrUOaBEAr4BWeZBn
G/87hhwZgNP69/KUchm714qd/PpOspCaUJ20x6PcmKujpAgca/f19HGMBjRawQMk
R9oaBwazuQT0l0rTTKmvpMEcrQQIcVWii3CZI56I56oqF8biGPD7AgEC
-----END DH PARAMETERS-----

------WebKitFormBoundaryrCtjrJ8JevAUKXAd
*/
result_e
requestParseMultipartForm(connection_s * con){

	buffer_s * buf = con->request.data;
	request_parser_s * parser = &(con->request.parser);
	const char * ptr = &buf->buffer[parser->body_n];
	const char * end = ptr + parser->body_len;
	const char * name;
	uint32_t name_len;
	const char * filename;
	uint32_t filename_len;
	const char * ctype;
	uint32_t ctype_len;
	const char * value;
	uint32_t value_len;
	kv_s * node;
	post_file_s * file;
	const char * tmp;
	const char * boundary = con->request.multipart_boundary.ptr;
	uint32_t boundary_len = con->request.multipart_boundary.len;

	const char * h;
	const char * eh = end - boundary_len - 1;

	//Граница не найдена
	if(*(int16_t *)ptr != 0x2D2D || !stringCompareN(ptr+2, boundary, boundary_len)) RETURN_ERROR(RESULT_ERROR, "First boundary not found");

	//Парсинг multipart/form-data
	while(ptr < end){

		name			= NULL;
		name_len		= 0;
		filename		= NULL;
		filename_len	= 0;
		ctype			= NULL;
		ctype_len		= 0;
		value			= NULL;
		value_len		= 0;

		//Пропускаем границу (длинна границы + 2 символа [-] в начале)
		ptr += boundary_len + 2;

		//Если после границы найдены два завершающих символа "--" - выход
		if(*(int16_t *)ptr == 0x2D2D) return RESULT_OK;

		//Если после границы не найдены символы перевода строки "\r\n"
		if(*(int16_t *)ptr != 0x0A0D) RETURN_ERROR(RESULT_ERROR, "RN not found");

		//Пропускаем символы перевода строки
		ptr+=2;

		//Не найдено Content-Disposition:
		if(!*ptr || !stringCompareCaseN(ptr, "Content-Disposition:", 20)) RETURN_ERROR(RESULT_ERROR, "Content-Disposition not found");

		//Пропускаем "Content-Disposition:"
		ptr+=20;

		//Пропускаем после "Content-Disposition:" слова "form-data" "attachment" и т.д. до символа [;]
		while(*ptr && *ptr!=';' && *ptr!='\n')ptr++;
		if(*ptr!=';') RETURN_ERROR(RESULT_ERROR, "*ptr!=';'");
		ptr++;

		//Пропускаем пробелы
		while(*ptr && *ptr==0x20)ptr++;

		//Ищем [name="]
		if(!*ptr || !stringCompareCaseN(ptr, "name=\"", 6)) RETURN_ERROR(RESULT_ERROR, "name=\" not found");
		ptr+=6;

		//Начало имени переменной
		name = ptr;

		//Ищем окончание имени переменной
		while(*ptr && *ptr!='"' && *ptr!='\n') ptr++;
		if(*ptr!='"') RETURN_ERROR(RESULT_ERROR, "\" not found");

		//Длинна имени переменной
		name_len = ptr - name;

		//Пропускаем завершающий ["]
		ptr++;

		//Пропускаем пробелы
		while(*ptr && *ptr==0x20)ptr++;

		//Если найден конец строки - то это текстовая переменная а не файл
		if(*(int32_t *)ptr == 0x0A0D0A0D) goto label_value;

		if(*ptr!=';') RETURN_ERROR(RESULT_ERROR, "; not found");
		ptr++;
		while(*ptr && *ptr==0x20)ptr++;

		//Ищем [filename="]
		if(!*ptr || !stringCompareCaseN(ptr, "filename=\"", 10)) RETURN_ERROR(RESULT_ERROR, "filename=\" not found: [%s]", ptr);
		ptr+=10;

		//Начало имени файла
		filename = ptr;

		//Ищем окончание имени файла
		while(*ptr && *ptr!='"' && *ptr!='\n') ptr++;
		if(*ptr!='"') RETURN_ERROR(RESULT_ERROR, "\" not found");

		//Длинна имени переменной
		filename_len = ptr - filename;

		//Пропускаем завершающий ["]
		ptr++;

		//Если после имени файла не найдены символы перевода строки "\r\n"
		if(*(int16_t *)ptr != 0x0A0D) RETURN_ERROR(RESULT_ERROR, "RN not found");

		//Пропускаем \r\n
		ptr+=2;

		//Поскольку это файл, то следующей строкой должно идти [Content-Type:] 
		if(!*ptr || !stringCompareCaseN(ptr, "Content-Type:", 13)) RETURN_ERROR(RESULT_ERROR, "Content-Type not found");

		//Пропускаем "Content-Type:"
		ptr+=13;

		//Пропускаем пробелы
		while(*ptr && *ptr==0x20)ptr++;

		//Начало типа контента
		ctype = ptr;

		//Ищем окончание строки
		while(*ptr && *ptr!='\r' && *ptr!='\n')ptr++;
		if(*ptr!='\r') RETURN_ERROR(RESULT_ERROR, "RN not found");

		//Длинна типа контента
		ctype_len = ptr - ctype;

		//Если не найден конец строки - ошибка
		if(*(int32_t *)ptr != 0x0A0D0A0D) RETURN_ERROR(RESULT_ERROR, "RNRN not found");


		//Определение значения переменной
		label_value:

		ptr+=4;

		//значение переменной
		value = ptr;

		//Поиск следующего вхождения границы
		//Не проверяем на \0, т.к. могут быть бинарные данные
		h = ptr;
		tmp = NULL;
		while(h <= eh){
			if(*(int16_t *)h == 0x2D2D && stringCompareN(h+2, boundary, boundary_len)){tmp = h; break;}
			++h;
		}

		//Граница не найдена
		if(tmp == NULL) RETURN_ERROR(RESULT_ERROR,"tmp == NULL : [%s]\n[%s]",ptr, eh);

		//Начало границы
		ptr = tmp;

		//Пропускаем \r\n перед границей
		//Если \r\n не найдено - ошибка
		tmp-=2;
		if(*(int16_t *)tmp != 0x0A0D) RETURN_ERROR(RESULT_ERROR,"RN not found");
		value_len = tmp - value;

		if(!name && !filename) continue;

		//Добавление переменной / файла в массив
		if(!name){
			name = filename;
			name_len = filename_len;
		}

		//POST переменная ключ = значение
		if(!filename){

			if(!con->request.post) con->request.post = kvNewRoot();
			node = kvAppend(con->request.post, name, name_len, KV_REPLACE);
			if(value && value_len>0) kvSetString(node, value, value_len);

		}
		//Файл
		else{
			if(value && value_len > 0){
				if(!con->request.files) con->request.files = kvNewRoot();
				file = (post_file_s *)mNewZ(sizeof(post_file_s));
				node = kvSetPointer(kvAppend(con->request.files, name, name_len, KV_REPLACE), file, mFree);
				file->filename.ptr = filename;
				file->filename.len = filename_len;
				file->content.ptr = value;
				file->content.len = value_len;
				if(ctype && ctype_len > 0){
					file->mimetype.ptr = ctype;
					file->mimetype.len = ctype_len;
				}else{
					file->mimetype.ptr = "application/octet-stream";
					file->mimetype.len = 24;
				}
			}
		}//Файл

	}//Парсинг multipart/form-data

	//Неожиданное окончание контента
	return RESULT_ERROR;

}//END: requestParseMultipartForm




/*
 * Функция возвращает текстовое описание метода запроса
 */
const char *
requestMethodString(request_method_e method){
	switch (method) {
		case HTTP_GET: return "GET";
		case HTTP_POST: return "POST";
		default:  return "UNDEFINED";
	}
}//END: requestMethodString



/*
 * Функция возвращает значение заголовка
 */
const char *
requestGetHeader(connection_s * con, const char * header){
	if(!con || !con->request.headers || !header) return NULL;
	kv_s * node = kvSearch(con->request.headers, header, 0);
	if(node) return node->value.v_string.ptr;
	return NULL;
}//END: requestGetHeader



/*
 * Получение значения переменной из массива GET POST или COOKIE, в зависимости от фильтра rv (по-умолчанию rv = "gpc")
 * где "g" - массив GET, "p" - массив POST , "c" = массив COOKIE
 */
const char *
requestGetGPC(connection_s * con, const char * name, const char * rv, uint32_t * olen){
	if(!rv) rv = "gpc";
	kv_s * vars;
	kv_s * node;
	uint32_t len, hash = hashString(name, &len);
	while(*rv){
		switch(*rv){
			case 'g': case 'G': vars = con->request.get; break;
			case 'p': case 'P': vars = con->request.post; break;
			case 'c': case 'C': vars = con->request.cookie; break;
			default: vars = NULL;
		}
		rv++;
		if(!vars) continue;
		node = kvSearchHash(vars, name, len, hash);
		if(!node || node->type != KV_STRING) continue;
		if(olen) *olen = node->value.v_string.len;
		return (const char *) node->value.v_string.ptr;
	}
	if(olen) *olen = 0;
	return NULL;
}//END: requestGetGPC



/*
 * Возвращает структуру, содержащую загруженный методом POST файл
 */
post_file_s *
requestGetFile(connection_s * con, const char * name){
	if(!con || con->request.request_method != HTTP_POST || con->request.post_method != POST_MULTIPART || !con->request.files || !name) return NULL;
	return (post_file_s *)kvGetPointerByPath(con->request.files, name, NULL);
}//END: requestGetFile





/*
 * Пытается найти локально запрошенный файл, и если файл найден - возвращает информацию о нем
 */
static_file_s *
requestStaticFileInfo(connection_s * con){
	static_file_s * f = NULL;
	struct stat st;
	string_s * filename = pathConcat(&con->server->config.public_html, &con->request.uri.path);
	//printf("FILE [%s]\n",filename->ptr);
	if(!fileStat(&st, filename->ptr)) goto label_error;

	int fd;
	if((fd = open(filename->ptr, O_RDONLY))==-1) goto label_error;

	f = (static_file_s *)mNewZ(sizeof(static_file_s));
	f->localfile = filename;
	f->fd = fd;
	f->etag = eTag(&st);
	kv_s * node;
	uint32_t n = 0;
	char * ptr = filename->ptr + filename->len;
	while(ptr > filename->ptr && *ptr != '.')ptr--,n++;
	//Поиск расширения файла и вычисление MIME типа
	if(*ptr == '.' && n>0){
		f->extension.ptr = (ptr+1);
		f->extension.len = n-1;
		node = kvSearch(con->server->config.mimetypes, f->extension.ptr, f->extension.len); 
		if(node){
			f->mimetype.ptr = node->value.v_string.ptr;
			f->mimetype.len = node->value.v_string.len;
		}else{
			f->mimetype.ptr = con->server->config.default_mimetype->ptr;
			f->mimetype.len = con->server->config.default_mimetype->len;
		} 
	}else{
		f->extension.ptr = "";
		f->extension.len = 0;
		f->mimetype.ptr = con->server->config.default_mimetype->ptr;
		f->mimetype.len = con->server->config.default_mimetype->len;
	}
	//Вычисление имени файла
	ptr = filename->ptr + filename->len; n = 0;
	while(ptr > filename->ptr && *ptr != '/')ptr--,n++;
	if(*ptr == '/' && n>0){
		f->filename.ptr = (ptr+1);
		f->filename.len = n-1;
	}else{
		goto label_error;
	}
	memcpy(&f->st, &st, sizeof(struct stat));
	return f;
	label_error:
	mStringFree(filename);
	if(f) mFree(f);
	return NULL;
}//END: requestStaticFileInfo



/*
 * Освобождает память, занятую структурой статичного файла
 */
void
requestStaticFileFree(static_file_s * f){
	close(f->fd);
	mStringFree(f->localfile);
	mStringFree(f->etag);
	mFree(f);
}//END: requestStaticFileFree


