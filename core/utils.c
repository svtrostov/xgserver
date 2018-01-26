/***********************************************************************
 * XG SERVER
 * core/utils.c
 * Прикладные функции
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/  


#include <openssl/sha.h>
#include "core.h"



/***********************************************************************
 * Хэш функции
 **********************************************************************/

//Вычисляет хеш строки по алгоритму SHA256
void 
hashSHA256(char * output, const char * input, uint32_t ilen){
	unsigned char hash[SHA256_DIGEST_LENGTH];
	if(!ilen) ilen = strlen(input);
	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, input, ilen);
	SHA256_Final(hash, &sha256);
	int i = 0;
	char * ptr = output;
	for(i = 0; i < SHA256_DIGEST_LENGTH; i++) ptr = hexFromChar(hash[i], ptr);
	*ptr = '\0';
}//END: hashSHA256



//Вычисляет хэш строки
uint32_t
hashString(const char * str, uint32_t * olen){
	if(!str){
		if(olen) *olen = 0;
		return 0;
	}
	register uint32_t hash = 0;
	register const char * ptr = str;
	while(*ptr){
		hash += (u_char)(*ptr);
		hash -= (hash << 13) | (hash >> 19);
		ptr++;
	}
	if(olen) *olen = (ptr - str);
	return hash;
}//END: hashString



//Вычисляет хэш n символов строки
uint32_t
hashStringN(const char * str, uint32_t ilen, uint32_t * olen){
	if(!str){
		if(olen) *olen = 0;
		return 0;
	}
	register uint32_t hash = 0;
	register uint32_t n = 0;
	while(*str && n < ilen){
		hash += (u_char)(*str);
		hash -= (hash << 13) | (hash >> 19);
		n++;str++;
	}
	if(olen) *olen = n;
	return hash;
}//END: hashStringN



//Вычисляет хэш строки без учета регистра
uint32_t
hashStringCase(const char * str, uint32_t * olen){
	if(!str){
		if(olen) *olen = 0;
		return 0;
	}
	register uint32_t hash = 0;
	register const char * ptr = str;
	while(*ptr){
		hash += (u_char)tolower((u_char)*ptr);
		hash -= (hash << 13) | (hash >> 19);
		ptr++;
	}
	if(olen) *olen = (ptr - str);
	return hash;
}//END: hashStringCase



//Вычисляет хэш n символов строки без учета регистра
uint32_t
hashStringCaseN(const char * str, uint32_t ilen, uint32_t * olen){
	if(!str){
		if(olen) *olen = 0;
		return 0;
	}
	register uint32_t hash = 0;
	register uint32_t n = 0;
	while(*str && n < ilen){
		hash += (u_char)tolower((u_char)*str);
		hash -= (hash << 13) | (hash >> 19);
		n++;str++;
	}
	if(olen) *olen = n;
	return hash;
}//END: hashStringCaseN



//Создает новую строку из n символов заданной строки, одновременно вычисляя хэш без учета регистра
char *
hashStringCloneCaseN(const char * str, uint32_t ilen, uint32_t * olen, uint32_t * ohash){
	char * new	= (char *)mNew(ilen+1);
	register char * ptr = new;
	register uint32_t hash = 0;
	register uint32_t n = 0;
	while(*str && n < ilen){
		hash += (u_char)tolower((u_char)*str);
		hash -= (hash << 13) | (hash >> 19);
		*ptr++ = *str++;
		n++;
	}
	*ptr = '\0';
	if(olen) *olen = n;
	if(ohash) *ohash = hash;
	return new;
}//END: hashStringCloneCaseN




//Копирует строку из n символов заданной строки, одновременно вычисляя хэш без учета регистра
char *
hashStringCopyCaseN(char * dst, const char * src, uint32_t ilen, uint32_t * olen, uint32_t * ohash){
	register char * ptr = dst;
	register uint32_t hash = 0;
	register uint32_t n = 0;
	while(*src && n < ilen){
		hash += (u_char)tolower((u_char)*src);
		hash -= (hash << 13) | (hash >> 19);
		*ptr++ = *src++;
		n++;
	}
	*ptr = '\0';
	if(olen) *olen = n;
	if(ohash) *ohash = hash;
	return dst;
}//END: hashStringCopyCaseN





/***********************************************************************
 * Математические функции
 **********************************************************************/


/*
 * Вычисление псевдо-случайного числа
 */
uint32_t
randomValue(uint32_t * seed){
	if(!*seed) *seed = nowNanoseconds();
	uint32_t next = *seed;
	uint32_t result;
	next *= 1103515245;
	next += 12345;
	result = (uint32_t) (next / 65536) % 2048;
	next *= 1103515245;
	next += 12345;
	result <<= 10;
	result ^= (uint32_t) (next / 65536) % 1024;
	next *= 1103515245;
	next += 12345;
	result <<= 10;
	result ^= (uint32_t) (next / 65536) % 1024;
	*seed = next;        
	return result;
}//END: randomValue






/***********************************************************************
 * Дата и время
 **********************************************************************/

/*
 * Усыпляет процесс / поток на sec количество секунд
 * Функция потоконезависимая, это означает, что вызывая функцию
 * в потоке A другие потоки будут продолжать работать.
 * Примечание: не использовать в многопоточном приложении 
 * функцию sleep(), поскольку она усыпляет весь процесс со всеми 
 * потоками, а не конкретный поток, из которого она была вызвана
 */
void
sleepSeconds(uint32_t sec){
	struct timespec ts, tmp;
	ts.tv_sec = sec;
	ts.tv_nsec = 0;
	nanosleep(&ts, &tmp);
	return;
}


/*
 * Усыпляет процесс / поток на usec количество миллисекунд
 * Функция потоконезависимая, это означает, что вызывая функцию
 * в потоке A другие потоки будут продолжать работать.
 * Примечание: не использовать в многопоточном приложении 
 * функцию sleep(), поскольку она усыпляет весь процесс со всеми 
 * потоками, а не конкретный поток, из которого она была вызвана
 */
void
sleepMilliseconds(uint32_t usec){
	struct timespec ts, tmp;
	ts.tv_sec = 0;
	ts.tv_nsec = usec*1000000;
	nanosleep(&ts, &tmp);
	return;
}


/*
 * Усыпляет процесс / поток на usec количество микросекунд
 * Функция потоконезависимая, это означает, что вызывая функцию
 * в потоке A другие потоки будут продолжать работать.
 * Примечание: не использовать в многопоточном приложении 
 * функцию sleep(), поскольку она усыпляет весь процесс со всеми 
 * потоками, а не конкретный поток, из которого она была вызвана
 */
void
sleepMicroseconds(uint32_t msec){
	struct timespec ts, tmp;
	ts.tv_sec = 0;
	ts.tv_nsec = msec*1000;
	nanosleep(&ts, &tmp);
	return;
}



/*
 * Возвращает строку, содержащую дату и время согласно заданного формата
 */
string_s *
datetimeFormat(time_t ts, const char * format){
	char tmp[256];
	struct tm tm;
	string_s * s = (string_s *)mNew(sizeof(string_s));
	localtime_r(&ts, &tm);
	s->len = strftime(tmp, sizeof(tmp)-1, format, &tm);
	s->ptr = stringCloneN(tmp, s->len, &s->len);
	return s;
}//END: datetimeFormat




/*
 * Функция возвращает текущее значение наносекунд
 */
uint32_t
nowNanoseconds(void){
	struct timespec tmsp;
	clock_gettime(CLOCK_REALTIME, &tmsp);
	return (uint32_t)tmsp.tv_nsec;
}//END: nowNanoseconds






/***********************************************************************
 * Преобразования чисел и строк
 **********************************************************************/



/*
 * Конвертирует INT64 число в строку
 */
char * 
intToString(int64_t n, uint32_t * olen){
	register int i, sign;
	char buf[NDIG];
	if((sign = n) < 0) n = -n;
	i = 0;
	do{
		buf[i++] = n % 10 + '0';
	}while ((n /= 10) > 0);
	if (sign < 0) buf[i++] = '-';
	buf[i] = '\0';
	stringReverse(buf, i);
	return stringCloneN(buf, i, olen);
}//END: intToString



/*
 * Конвертирует INT64 число в строку
 */
uint32_t
intToStringPtr(int64_t n, char * result){
	register int i, sign;
	if((sign = n) < 0) n = -n;
	i = 0;
	do{
		result[i++] = n % 10 + '0';
	}while ((n /= 10) > 0);
	if (sign < 0) result[i++] = '-';
	result[i] = '\0';
	stringReverse(result, i);
	return (uint32_t)i;
}//END: intToStringPtr



/*
 * Конвертирует DOUBLE число в строку
 */
char *
doubleToString(double arg, int ndigits, uint32_t * olen){

	register int r2;
	double fi, fj;
	register char *p, *p1;
	char buf[NDIG];
	int sign = 0, dot_pos = -1;
	double modf();
	char * result;
	uint32_t result_n;

	ndigits = max(0,min(ndigits, 20));

	r2 = 0;
	p = &buf[0];
	if (arg < 0){
		sign = 1;
		arg = -arg;
	}
	arg = modf(arg, &fi);
	p1 = &buf[NDIG];
	if (fi != 0){
		p1 = &buf[NDIG];
		while (fi != 0) {
			fj = modf(fi/10, &fi);
			*--p1 = (int)((fj+.03)*10) + '0';
			r2++;
		}
		while (p1 < &buf[NDIG])
			*p++ = *p1++;
	} else if (arg > 0) {
		while ((fj = arg*10) < 1) {
			arg = fj;
			r2--;
		}
	}
	p1 = &buf[ndigits];
	p1 += r2;
	if (p1 < &buf[0]) {
		buf[0] = '\0';
		goto label_end;
	}
	dot_pos = r2;
	while (p <= p1 && p < &buf[NDIG]){
		arg *= 10;
		arg = modf(arg, &fj);
		*p++ = (int)fj + '0';
	}
	if (p1 >= &buf[NDIG]) {
		buf[NDIG-1] = '\0';
		goto label_end;
	}
	p = p1;
	*p1 += 5;
	while (*p1 > '9') {
		*p1 = '0';
		if (p1 > buf){
			++*--p1;
		}else{
			dot_pos++;
			*p1 = '1';
			if (p > buf) *p = '0';
			p++;
		}
	}
	*p = '\0';

	label_end:

	result_n = (p - buf) +  (ndigits == 0 ? 0 : (dot_pos > 0 ? 1 : 2));
	if(!result_n){
		result_n = 1;
		sign = 0;
		buf[0]='0';
		buf[1]='\0';
	}else{
		result_n += sign;
	}
	result = (char *)mNew(result_n + 1);
	p = result;
	p1 = buf;
	if(sign) *p++ = '-';
	if(ndigits == 0){
		stringCopy(p, p1);
	}else{
		if(dot_pos > 0){
			p1 += stringCopyN(p, p1, dot_pos);
			p[dot_pos] = '.';
			stringCopy(p+dot_pos+1, p1);
		}else{
			*p++ = '0';
			*p++ = '.';
			stringCopy(p, p1);
		}
	}
	if(olen)*olen = result_n;
	return result;
}//END: doubleToString



/*
 * Конвертирует DOUBLE число в строку
 */
uint32_t
doubleToStringPtr(double arg, int ndigits, char * result){

	register int r2;
	double fi, fj;
	register char *p, *p1;
	char buf[NDIG];
	int sign = 0, dot_pos = -1;
	double modf();
	uint32_t result_n;

	ndigits = max(0,min(ndigits, 20));

	r2 = 0;
	p = &buf[0];
	if (arg < 0){
		sign = 1;
		arg = -arg;
	}
	arg = modf(arg, &fi);
	p1 = &buf[NDIG];
	if (fi != 0){
		p1 = &buf[NDIG];
		while (fi != 0) {
			fj = modf(fi/10, &fi);
			*--p1 = (int)((fj+.03)*10) + '0';
			r2++;
		}
		while (p1 < &buf[NDIG])
			*p++ = *p1++;
	} else if (arg > 0) {
		while ((fj = arg*10) < 1) {
			arg = fj;
			r2--;
		}
	}
	p1 = &buf[ndigits];
	p1 += r2;
	if (p1 < &buf[0]) {
		buf[0] = '\0';
		goto label_end;
	}
	dot_pos = r2;
	while (p <= p1 && p < &buf[NDIG]){
		arg *= 10;
		arg = modf(arg, &fj);
		*p++ = (int)fj + '0';
	}
	if (p1 >= &buf[NDIG]) {
		buf[NDIG-1] = '\0';
		goto label_end;
	}
	p = p1;
	*p1 += 5;
	while (*p1 > '9') {
		*p1 = '0';
		if (p1 > buf){
			++*--p1;
		}else{
			dot_pos++;
			*p1 = '1';
			if (p > buf) *p = '0';
			p++;
		}
	}
	*p = '\0';

	label_end:

	result_n = (p - buf) +  (ndigits == 0 ? 0 : (dot_pos > 0 ? 1 : 2));
	if(!result_n){
		result_n = 1;
		sign = 0;
		buf[0]='0';
		buf[1]='\0';
	}else{
		result_n += sign;
	}
	p = result;
	p1 = buf;
	if(sign) *p++ = '-';
	if(ndigits == 0){
		stringCopy(p, p1);
	}else{
		if(dot_pos > 0){
			p1 += stringCopyN(p, p1, dot_pos);
			p[dot_pos] = '.';
			stringCopy(p+dot_pos+1, p1);
		}else{
			*p++ = '0';
			*p++ = '.';
			stringCopy(p, p1);
		}
	}
	return result_n;
}//END: doubleToStringPtr



/*
 * Преобразует текстовую строку в число типа int64_t
 */
int64_t
stringToInt64(const char *nptr, const char ** rptr){
	int64_t value = 0;
	char sign = *nptr;
	if (*nptr == '-' || *nptr == '+') nptr++;
	while (*nptr>='0' && *nptr<='9'){
		value = 10 * value + (*nptr - '0');
		nptr++;
	}
	if(rptr) *rptr = nptr;
	if (sign == '-') return -value;
	else return value;
}//END: stringToInt64



/*
 * Проверяет, является ли переданная строка числом типа INT
 */
bool
stringIsInt(const char *p){
	register const char *ptr = p;
	if(*ptr =='+' || *ptr=='-') ptr++;
	for(; *ptr; ptr++){
		if(*ptr<'0'||*ptr>'9') return false;
	}
	return (ptr != p ? true : false);
}//END: stringIsInt



/*
 * Проверяет, является ли переданная строка числом типа UNSIGNED INT
 */
bool
stringIsUnsignedInt(const char *p) {
	register const char *ptr = p;
	for(; *ptr; ptr++){
		if(*ptr<'0'||*ptr>'9') return false;
	}
	return (ptr != p ? true : false);
}//END: stringIsUnsignedInt



/*
 * Проверяет, является ли переданная строка числом типа DOUBLE
 */
bool
stringIsDouble(const char *p){
	bool dot=false;
	register const char *ptr = p;
	if(*ptr =='+' || *ptr=='-') ptr++;
	for(; *ptr; ptr++){
		if(*ptr=='.'){
			if(dot)  return false;
			dot = true; continue;
		}
		if(*ptr<'0'||*ptr>'9') return false;
	}
	return (ptr != p ? true : false);
}//END: stringIsDouble











/***********************************************************************
 * Работа со строками
 **********************************************************************/


/*
 * Функция генерирует случайную строковую последовательность запрошенного размера и возвращает указатель на ее начало
 */
char *
stringRandom(uint32_t count, char * str){
	uint32_t seed 	= nowNanoseconds();
	if(!str) str = (char *) mNew(count+1);
	char * p = str;
	while(count-- > 0){
		*p++ = _us_chars[randomValue(&seed)%sizeof(_us_chars)];
	}
	*p = '\0';
	return str;
}//END: stringRandom



/*
 * Функция копирования символов из одной строки в другую 
 * возвращает фактически скопированное количество символов
 */
uint32_t
stringCopy(char * to, const char * from){
	if(!from) return 0;
	register char * ptr = to;
	while(*from) *ptr++ = *from++;
	*ptr = '\0';
	return (ptr - to);
}//END: stringCopy



/*
 * Функция копирования определенного количества символов из одной строки в другую
 * возвращает фактически скопированное количество символов
 */
uint32_t
stringCopyN(char * to, const char * from, uint32_t len){
	if(!from) return 0;
	register size_t n = 0;
	while(*from && n<len){
		*to++ = *from++;
		++n;
	}
	*to = '\0';
	return n;
}//END: stringCopyN



/*
 * Функция копирования определенного количества символов из одной строки в другую
 * возвращает фактически скопированное количество символов, при этом текст преобразуется к нижнему регистру
 */
uint32_t
stringCopyCaseN(char * to, const char * from, uint32_t len){
	if(!from) return 0;
	register size_t n = 0;
	while(*from && n<len){
		*to++ = (char)tolower((unsigned char)*from++);
		++n;
	}
	*to = '\0';
	return n;
}//END: stringCopyCaseN



/*
 * Создает новую строку и копирует в нее содержимое строки from,
 */
char *
stringClone(const char * from, uint32_t * len){
	if(!from) return NULL;
	size_t n 	= strlen(from);
	char * new	= mNew(n+1);
	if (new == NULL) return NULL;
	if(len) *len = n;
	new[n] = '\0';
	return (char *)memcpy(new, from, n);
}//END: stringClone



/*
 * Создает новую строку и копирует в нее содержимое ilen символов из строки from, 
 * возвращает в olen длинну полученной строки, если olen != NULL
 */
char *
stringCloneN(const char * from, uint32_t ilen, uint32_t * olen){
	if(!from) return NULL;
	char * new	= (char *)mNew(ilen+1);
	uint32_t n	= stringCopyN(new, from, ilen);
	if(olen) *olen = n;
	return new;
}//END: stringCloneN



/*
 * Создает новую строку и копирует в нее содержимое ilen символов из строки from, при этом текст преобразуется к нижнему регистру
 * возвращает в olen длинну полученной строки, если olen != NULL
 */
char *
stringCloneCaseN(const char * from, uint32_t ilen, uint32_t * olen){
	if(!from) return NULL;
	char * new	= (char *)mNew(ilen+1);
	uint32_t n	= stringCopyCaseN(new, from, ilen);
	if(olen) *olen = n;
	return new;
}//END: stringCloneCaseN



/*
 * Создает новую строку и копирует в нее содержимое структуры const_string_s, 
 * возвращает в olen длинну полученной строки, если olen != NULL
 */
char *
stringCloneStringS(const_string_s * from, uint32_t * olen){
	if(!from || !from->ptr || !from->len) return NULL;
	char * new	= (char *)mNew(from->len+1);
	uint32_t n	= stringCopyN(new, from->ptr, from->len);
	if(olen) *olen = n;
	return new;
}//END: stringCloneStringS



/*
 * Переворачивает N символов строки (зекальное отражение)
 */
uint32_t
stringReverse(char * s, uint32_t ilen){
	register int i, j;
	register char c;
	if(!ilen) ilen = strlen(s);
	for (i = 0, j = ilen-1; i<j; i++, j--){
		c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
	return ilen;
}//END: xg_reverse




/*
 * Разбивает стоку, используя в качестве разделителя символ delimer,
 * возвращает указатель на начало нового вхождения и также в *len - длинну строки
 * Пример:
 * char * my_text = "Hello! How are you? Thanks, i'm fine! Ok, buy.";
 * char * my_delimers = "!?";
 * char * my_ptr = my_text;
 * char * result_ptr;
 * unsigned int result_len;
 * while ( ( result_ptr = _explode(&my_ptr, my_delimers, &result_len) ) != NULL){
 * 		...
 * }
 * Результат работы функции, следующие вхождения:
 * "Hello"
 * " How are you"
 * " Thanks, i'm fine"
 * " Ok, buy."
 */
const char *
stringExplode(const char **str, u_char delimer, size_t * len){
	const char * ptr = *str;
	const char * result;
	//Если ptr = NULL либо *ptr = 0x00
	if(!ptr || !*ptr) return NULL;

	//Поиск вхождения delimer
	while(*ptr && *ptr != delimer) ++ptr;
	if(len) *len = (size_t)(ptr - *str);

	//Если значение *ptr = разделителю
	if(*ptr == delimer && delimer!='\0') ++ptr;

	result	= *str;
	*str	= ptr;
	return result;
}//END: stringExplode



/*
 * Сравнивает две строки
 */
inline bool 
stringCompare(const char *str1, const char *str2){
	while ( *str1 || *str2){
		if( *str1 != *str2 ) return false;
		++str1;
		++str2;
	}
	return true;
}



/*
 * Сравнивает две строки без учета регистра
 */
inline bool 
stringCompareCase(const char *str1, const char *str2){
	while ( *str1 || *str2){
		if( tolower((int)*str1) != tolower((unsigned char)*str2) ) return false;
		++str1;
		++str2;
	}
	return true;
}



/*
 * Сравнивает n символов двух строк
 */
inline bool 
stringCompareN(const char *str1, const char *str2, uint32_t len){
	while ( (*str1 || *str2) && len>0){
		if( *str1 != *str2 ) return false;
		++str1;
		++str2;
		--len;
	}
	return true;
}



/*
 * Сравнивает n символов двух строк без учета регистра
 */
inline bool 
stringCompareCaseN(const char *str1, const char *str2, uint32_t len){
	while ( (*str1 || *str2) && len>0){
		if( tolower((int)*str1) != tolower((unsigned char)*str2) ) return false;
		++str1;
		++str2;
		--len;
	}
	return true;
}



/*
 * Функция проверяет, является ли строка либо часть строки в количестве символов count записью в HEX формате.
 * если строка в HEX формате - возвращает TRUE, либо FALSE если нет
 */
inline bool
stringIsHex(const char * str, uint32_t count){
	register int len			= 0;
	register const char * ptr 	= str;
	while(*ptr != 0 && (!count || len < count)){
		if( (*ptr < '0' || *ptr > '9') &&
			(*ptr < 'a' || *ptr > 'f') &&
			(*ptr < 'A' || *ptr > 'F') ) return false;
		++ptr;
		++len;
	}
	return (!count || len == count ? true : false);
}//END: stringIsHex



/*
 * Проверяет, нявляется ли символ зарезервированным или нет
 */
bool
charIsUnreserved(u_char in){
	return (!unreserved_chars[in] ? true : false);
	/*
	switch (in) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
		case 'a': case 'b': case 'c': case 'd': case 'e':
		case 'f': case 'g': case 'h': case 'i': case 'j':
		case 'k': case 'l': case 'm': case 'n': case 'o':
		case 'p': case 'q': case 'r': case 's': case 't':
		case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
		case 'A': case 'B': case 'C': case 'D': case 'E':
		case 'F': case 'G': case 'H': case 'I': case 'J':
		case 'K': case 'L': case 'M': case 'N': case 'O':
		case 'P': case 'Q': case 'R': case 'S': case 'T':
		case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
		case '-': case '.': case '_': case '~':
		return true;
		default:
		break;
	}
	return false;
	*/
}//END: charIsUnreserved



/*
 * Проверяет, находится ли указанный символ в массиве символов
 */
inline bool
charExists(char c, const char * array){
	while(*array){
		if(c == *array++) return true;
	}
	return false;
}//END: charExists



/*
 * Проверяет, является ли указатель на текстовую строку 
 * нулевым либо значение по данному указателю равно 0x00
 */
inline bool
charIsZero(char * ptr){
	return (!ptr || !*ptr ? true : false);
}//END: charIsZero





/*
 * Преобразует HEX представление в символ типа char
 */
inline u_char
charFromHex(const char * str){
	char ch;
	if(str[0]>='A')
		ch = ((str[0]&0xdf)-'A')+10;
	else
		ch = str[0] - '0';
	ch <<= 4;
	if(str[1]>='A')
		ch += ((str[1]&0xdf)-'A')+10;
	else
		ch += str[1] - '0';
	return ch;
}//END: charFromHex




/*
 * Преобразует символ типа char в HEX представление
 */
inline char * 
hexFromChar(u_char n, char *str){
	*str++ = hexTable[n >> 4];
	*str++ = hexTable[n & 0xf];
	return str;
}//END: hexFromChar



/*
 * Приводит символ к нижнему регистру
 */
inline u_char
charToLower(u_char ch){
	return (ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
}//END: charToLower



/*
 * Приводит символ к верхнему регистру
 */
inline u_char
charToUpper(u_char ch){
	return (ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
}//END: charToUpper



/*
 * Ищет первое вхождение символа c в первых n символах строки str
 */
inline char *
charSearchN(const char *str, char c, size_t n){
	if (!str) return NULL;
	while(*str && n-- > 0){
		if (*str == c) return ((char *)str);
		str++;
	}
	return NULL;
}//END: charToUpper






/***********************************************************************
 * Кодирование и декодирование строки
 **********************************************************************/
 
#define CHECK_LEN(pos, chars_need) ((str_len - (pos)) >= (chars_need))
#define MB_FAILURE(pos, advance) do { \
	*cursor = pos + (advance); \
	*status = -1; \
	return 0; \
} while (0)
#define utf8_trail(c) ((c) >= 0x80 && (c) <= 0xBF)
#define utf8_lead(c)  ((c) < 0x80 || ((c) >= 0xC2 && (c) <= 0xF4))

/*
 * Возвращает следующий UTF8 символ
 */
static uint32_t
_utf8NextChar(const unsigned char *str, size_t str_len, size_t *cursor, int *status){
	size_t pos = *cursor;
	uint32_t this_char = 0;
	*status = 0;
	if (!CHECK_LEN(pos, 1)) MB_FAILURE(pos, 1);
	unsigned char c;
	c = str[pos];
	if (c < 0x80){
		this_char = c;
		pos++;
	} else if (c < 0xc2) {
		MB_FAILURE(pos, 1);
	} else if (c < 0xe0) {
		if (!CHECK_LEN(pos, 2)) MB_FAILURE(pos, 1);

		if (!utf8_trail(str[pos + 1])) {
			MB_FAILURE(pos, utf8_lead(str[pos + 1]) ? 1 : 2);
		}
		this_char = ((c & 0x1f) << 6) | (str[pos + 1] & 0x3f);
		if (this_char < 0x80) {
			MB_FAILURE(pos, 2);
		}
		pos += 2;
	} else if (c < 0xf0) {
		size_t avail = str_len - pos;

		if (avail < 3 ||
				!utf8_trail(str[pos + 1]) || !utf8_trail(str[pos + 2])) {
			if (avail < 2 || utf8_lead(str[pos + 1]))
				MB_FAILURE(pos, 1);
			else if (avail < 3 || utf8_lead(str[pos + 2]))
				MB_FAILURE(pos, 2);
			else
				MB_FAILURE(pos, 3);
		}

		this_char = ((c & 0x0f) << 12) | ((str[pos + 1] & 0x3f) << 6) | (str[pos + 2] & 0x3f);
		if (this_char < 0x800) {
			MB_FAILURE(pos, 3);
		} else if (this_char >= 0xd800 && this_char <= 0xdfff) {
			MB_FAILURE(pos, 3);
		}
		pos += 3;
	} else if (c < 0xf5) {
		size_t avail = str_len - pos;
		if (avail < 4 ||
				!utf8_trail(str[pos + 1]) || !utf8_trail(str[pos + 2]) ||
				!utf8_trail(str[pos + 3])) {
			if (avail < 2 || utf8_lead(str[pos + 1]))
				MB_FAILURE(pos, 1);
			else if (avail < 3 || utf8_lead(str[pos + 2]))
				MB_FAILURE(pos, 2);
			else if (avail < 4 || utf8_lead(str[pos + 3]))
				MB_FAILURE(pos, 3);
			else
				MB_FAILURE(pos, 4);
		}
		this_char = ((c & 0x07) << 18) | ((str[pos + 1] & 0x3f) << 12) | ((str[pos + 2] & 0x3f) << 6) | (str[pos + 3] & 0x3f);
		if (this_char < 0x10000 || this_char > 0x10FFFF) {
			MB_FAILURE(pos, 4);
		}
		pos += 4;
	} else {
		MB_FAILURE(pos, 1);
	}
	*cursor = pos;
	return this_char;
}


/*
 * Преобразует UTF8 в UTF16
 */
int 
utf8To16(unsigned short *utf16, const char utf8[], int len){

	size_t pos = 0, us;
	int j, status;

	if (utf16) {
		//Конвертация в UTF-8
		for (j=0 ; pos < len ; j++) {
			us = _utf8NextChar((const unsigned char *)utf8, len, &pos, &status);
			if (status != 0) {
				return -1;
			}
			//Из http://en.wikipedia.org/wiki/UTF16
			if (us >= 0x10000) {
				us -= 0x10000;
				utf16[j++] = (unsigned short)((us >> 10) | 0xd800);
				utf16[j] = (unsigned short)((us & 0x3ff) | 0xdc00);
			} else {
				utf16[j] = (unsigned short)us;
			}
		}
	} else {
		//Просто проверка корректности UTF8 строки
		for (j=0 ; pos < len ; j++) {
			us = _utf8NextChar((const unsigned char *)utf8, len, &pos, &status);
			if (status != 0) {
				return -1;
			}
			if (us >= 0x10000) {
				j++;
			}
		}
	}
	return j;
}//END: utf8To16



/*
 * Преобразует UTF16 в UTF8
 */
void
utf16To8(buffer_s * buf, unsigned short utf16){
	if (utf16 < 0x80)
	{
		bufferAddChar(buf, (unsigned char)utf16);
	}
	else if (utf16 < 0x800)
	{
		bufferAddChar(buf, 0xc0 | (utf16 >> 6));
		bufferAddChar(buf, 0x80 | (utf16 & 0x3f));
	}
	else if ((utf16 & 0xfc00) == 0xdc00
				&& buf->index >= 3
				&& ((unsigned char) buf->buffer[buf->index - 3]) == 0xed
				&& ((unsigned char) buf->buffer[buf->index - 2] & 0xf0) == 0xa0
				&& ((unsigned char) buf->buffer[buf->index - 1] & 0xc0) == 0x80)
	{
		/* found surrogate pair */
		uint32_t utf32;

		utf32 = (((buf->buffer[buf->index - 2] & 0xf) << 16)
					| ((buf->buffer[buf->index - 1] & 0x3f) << 10)
					| (utf16 & 0x3ff)) + 0x10000;
		buf->index -= 3;

		bufferAddChar(buf, (unsigned char) (0xf0 | (utf32 >> 18)));
		bufferAddChar(buf, 0x80 | ((utf32 >> 12) & 0x3f));
		bufferAddChar(buf, 0x80 | ((utf32 >> 6) & 0x3f));
		bufferAddChar(buf, 0x80 | (utf32 & 0x3f));
	}
	else
	{
		bufferAddChar(buf, 0xe0 | (utf16 >> 12));
		bufferAddChar(buf, 0x80 | ((utf16 >> 6) & 0x3f));
		bufferAddChar(buf, 0x80 | (utf16 & 0x3f));
	}
}//END: utf16To8




/*
 * Функция преобразует числовое значение Юникода в символы UTF 8,16,32
 * r - буфер, куда записываются полученные символы
 * wc - числовое значение по юникоду
 * n - максимальное количество символов, которое можно записать в буфер (количество символов в кодировке)
 * в случае ошибки функция возвращает -1
 */
int32_t
unicodeToUtf8(char * r, uint32_t wc, int32_t n){
	uint32_t count;
	if (wc < 0x80) count = 1;
	else if (wc < 0x800) count = 2;
	else if (wc < 0x10000) count = 3;
	else if (wc < 0x200000) count = 4;
	else if (wc < 0x4000000) count = 5;
	else if (wc <= 0x7fffffff) count = 6;
	else return -1;
	if (n < count) return -1;
	switch (count) {
		case 6: r[5] = 0x80 | (wc & 0x3f); wc = wc >> 6; wc |= 0x4000000;
		case 5: r[4] = 0x80 | (wc & 0x3f); wc = wc >> 6; wc |= 0x200000;
		case 4: r[3] = 0x80 | (wc & 0x3f); wc = wc >> 6; wc |= 0x10000;
		case 3: r[2] = 0x80 | (wc & 0x3f); wc = wc >> 6; wc |= 0x800;
		case 2: r[1] = 0x80 | (wc & 0x3f); wc = wc >> 6; wc |= 0xc0;
		case 1: r[0] = wc;
	};
	return count;
}//END: unicodeToUtf8



/*
 * Преобразует строку из символьного представления в JSON представление, возвращает новую строку,
 * В случае ошибки возвращает NULL, иначе - указатель на закодированную строку
 * str - исходная строка
 * slen - количество символов, которое надо кодировать, либо 0 чтобы закодировать строку целиком
*/
buffer_s *
encodeJson(const char * str, uint32_t ilen, buffer_s * buf){

	if(!ilen) ilen = strlen(str);
	int pos = 0, ulen = 0;
	unsigned short us;
	unsigned short *utf16 = (unsigned short *)mNewZ(sizeof(unsigned short) * ilen);
	char uXXXX[7] = {'\\','u','\0','\0','\0','\0','\0'};

	ulen = utf8To16(utf16, str, ilen);
	if (ulen <= 0) {
		if (utf16) mFree(utf16);
		return NULL;
	}

	//Максимальная длинна закодированной строки
	//при учете, что все символы в кодировке UTF
	//равняется шести кратам от исходного размера
	if(!buf) buf = bufferCreate(ulen*6 + 1);

	while (pos < ulen){
		us = utf16[pos++];
		switch (us){
			case '"'	: bufferAddStringN(buf, "\\\"", 2); break;
			case '\\'	: bufferAddStringN(buf, "\\\\", 2); break;
			case '/'	: bufferAddStringN(buf, "\\/", 2); break;
			case '\b'	: bufferAddStringN(buf, "\\b", 2); break;
			case '\f'	: bufferAddStringN(buf, "\\f", 2); break;
			case '\n'	: bufferAddStringN(buf, "\\n", 2); break;
			case '\r'	: bufferAddStringN(buf, "\\r", 2); break;
			case '\t'	: bufferAddStringN(buf, "\\t", 2); break;
			default:
				if (us >= ' ' && ((us & 127) == us)){
					bufferAddChar(buf, (unsigned char) us);
				} else {
					uXXXX[2] = digits[(us & 0xf000) >> 12];
					uXXXX[3] = digits[(us & 0xf00)  >> 8];
					uXXXX[4] = digits[(us & 0xf0)   >> 4];
					uXXXX[5] = digits[(us & 0xf)];
					bufferAddStringN(buf, uXXXX, 6);
					/*
					bufferAddStringN(buf, "\\u", 2);
					bufferAddChar(buf, digits[(us & 0xf000) >> 12]);
					bufferAddChar(buf, digits[(us & 0xf00)  >> 8]);
					bufferAddChar(buf, digits[(us & 0xf0)   >> 4]);
					bufferAddChar(buf, digits[(us & 0xf)]);
					*/
				}
			break;
		}
	}
	if (utf16) mFree(utf16);

	return buf;
}//END: encodeJson



/*
 * Преобразует строку из JSON представления в символьного представление, возвращает новую строку,
 * В случае ошибки возвращает NULL, иначе - указатель на закодированную строку
 * str - исходная строка
 * slen - количество символов, которое надо кодировать, либо 0 чтобы закодировать строку целиком
*/
buffer_s *
decodeJson(const char * str, uint32_t ilen, buffer_s * buf){

	register const char	* ptr = str;
	register const char	* end = (!ilen ? NULL : str + ilen);
	if(!buf) buf = bufferCreate((ilen > 0 ? ilen + 1 : 0));
	register unsigned short us;

	while(*ptr && (!end || ptr < end)){
		if(*ptr == '\\'){
			ptr++; if(end && ptr >= end) goto label_end;
			switch(*ptr){
				//Предположительно символ Unicode
				case 'u':
				case 'U':
					if(stringIsHex(ptr+1,4) && (!end || ptr+5 < end)){
						us  = charFromHex(ptr+1) * 256;
						us += charFromHex(ptr+3);
						if(!us) goto label_end;
						utf16To8(buf, us);
						ptr+=4;
					}else{
						bufferAddChar(buf, (unsigned char)*ptr); break;
					}
				break;
				case 'b': bufferAddChar(buf, '\b'); break;
				case 'f': bufferAddChar(buf, '\f'); break;
				case 'n': bufferAddChar(buf, '\n'); break;
				case 'r': bufferAddChar(buf, '\r'); break;
				case 't': bufferAddChar(buf, '\t'); break;
				case '\\': bufferAddChar(buf, '\\'); break;
				case '/': bufferAddChar(buf, '/'); break;
				case '\"': bufferAddChar(buf, '\"'); break;
				case '0': case 0: goto label_end; break;
				default: bufferAddChar(buf, (unsigned char)*ptr); break;
			}
		}else{
			bufferAddChar(buf, (unsigned char)*ptr);
		}
		ptr++;
	}// while

	label_end:
	return buf;
}//END: decodeJson




/*
 * Преобразует строку из символьного представления в HEX представление, возвращает новую строку,
 * В случае ошибки возвращает NULL, иначе - указатель на закодированную строку
 * str - исходная строка
 * slen - количество символов, которое надо кодировать, либо 0 чтобы закодировать строку целиком
*/
buffer_s *
encodeUrlQuery(const char * str, uint32_t ilen, buffer_s * buf){
	if(!str) return NULL;
	register const char	* ptr = str;
	register const char	* end = (ilen > 0 ? str + ilen : NULL);

	//Максимальная длинна закодированной строки
	//при учете, что все символы в кодировке UTF
	//равняется трем кратам от исходного размера
	if(!buf) buf = bufferCreate((ilen > 0 ? ilen*3 + 1 : 0));

	while (*ptr && (!end || ptr < end)){

		if(*ptr == ' '){
			bufferAddChar(buf, '+');
		}
		else
		if(charIsUnreserved((unsigned char)*ptr)){
			bufferAddChar(buf, *ptr);
		}else{
			bufferAddChar(buf, '%');
			bufferAddHex(buf, *ptr);
		}
		ptr++;
	}

	return buf;
}//END: encodeUrlQuery




/*
 * Декодирует строку из HEX представления в символьное
 * Функция может декодировать как представлени типа %3A%2F%2F
 * так и представления в формате Юникода, типа %u041F%u0440%u0438 или \u041F\u0440\u0438
 */
buffer_s *
decodeUrlQuery(const char * str, uint32_t ilen, buffer_s * buf){

	register const char	* ptr = str;
	register const char	* end = (!ilen ? NULL : str + ilen);
	if(!buf) buf = bufferCreate((ilen > 0 ? ilen + 1 : 0));
	register unsigned short us;

	while(*ptr && (!end || ptr < end)){
		if(*ptr=='+'){
			bufferAddChar(buf, ' ');
			ptr++;
			continue;
		}
		else
		if(*ptr=='%' && stringIsHex(ptr+1, 2) && (!end || ptr+2 < end)){
			bufferAddChar(buf, charFromHex(ptr+1));
			ptr += 3;
			continue;
		}
		//Предположительно символ Unicode
		else
		if((*ptr == '%' || *ptr == '\\') && (*(ptr+1)=='u' || *(ptr+1)=='U') && stringIsHex(ptr+2,4) && (!end || ptr+5 < end)){
			us  = charFromHex(ptr+2) * 256;
			us += charFromHex(ptr+4);
			if(!us) goto label_end;
			utf16To8(buf, us);
			ptr+=6;
			continue;
		}

		bufferAddChar(buf, (unsigned char)*ptr);
		ptr++;
	}// while

	label_end:
	return buf;
}//END: decodeUrlQuery
/*
char *
stringDecodeUrl(const char * str, uint32_t ilen, uint32_t * olen){

	uint32_t	len		= 0;
	int32_t 	n		= 0;
	char *		result 	= stringCloneN(str, ilen, NULL);
	char * 		rptr = result;
	char *		ptr = result;

	while(*ptr){
		if(*ptr=='+') *ptr = 0x20;
		else
		if(*ptr=='%' && stringIsHex((ptr+1), 2)){
			*rptr++ = charFromHex(ptr+1);
			ptr += 3;
			++len;
			continue;
		}
		else
		if((*ptr=='%'||*ptr=='\\') && (*(ptr+1)=='u' || *(ptr+1)=='U') && stringIsHex((ptr+2), 4)){
			n  = charFromHex(ptr+2) * 256;
			n += charFromHex(ptr+4);
			unicodeToUtf8(rptr, n, 2);
			rptr += 2;
			len	 += 2;
			ptr  += 6;
			continue;
		}
		*rptr++ = *ptr++;
		++len;
	}
	*rptr = 0x00;
	if(olen) *olen = len;
	return (len != ilen ? mRealloc(result, len+1) : result);
}//END: stringDecodeUrl

*/



/***********************************************************************
 * Работа с файловой системой
 **********************************************************************/

/*
 * Проверяет существование директории
 */
bool
dirExists(const char * dirname){
	struct stat st;
	return stat(dirname, &st) == 0 && (st.st_mode & S_IFDIR);
}



/*
 * Возвращает путь к текущей рабочей директории
 */
char *
getCurrentDir(uint32_t * olen){
	char * cwd;
	char buf[PATH_MAX + 1];
	cwd = getcwd(buf, PATH_MAX);
	if(cwd == NULL) return NULL;
	return stringClone(buf, olen);
}



/*
 * Получает информацию о файле и записывает ее в структуру struct stat, возвращает false в случае ошибки
 */
bool
fileStat(struct stat * st, const char * filename){
	return stat(filename, st) == 0 && (st->st_mode & S_IFREG) ? true : false;
}



/*
 * Проверяет существование файла
 */
bool
fileExists(const char * filename){
	struct stat st;
	return stat(filename, &st) == 0 && (st.st_mode & S_IFREG);
}



/*
 * Возвращает размер файла
 */
int64_t 
fileSize(const char * filename){
	struct stat st;
	if(stat(filename, &st)!=0) return -1;
	return (int64_t)st.st_size;
}



/*
 * Возвращает полный путь к файлу из относительного
 */
char *
fileRealpath(const char * filename, uint32_t * olen){
	if(!filename) return NULL;
	char buf[PATH_MAX+1];
	if(!realpath(filename, buf)) return NULL;
	return stringClone(buf, olen);
}//END: fileRealpath



/*
 * Читает из файла filename ilen байт, начиная с позиции offset
 */
char *
fileRead(const char * filename, int64_t offset, int64_t ilen, int64_t * olen){
	FILE * fp = fopen(filename, "rb");
	if(!fp) return NULL;
	fseek(fp, 0, SEEK_END);
	int64_t flen = (int64_t)ftell(fp);
	if(!ilen) ilen = flen - offset;
	if(ilen < 0 || ilen + offset > flen){
		fclose(fp);
		return NULL;
	}
	fseek(fp, offset, SEEK_SET);
	char * buffer = mNew(ilen + 1);
	buffer[ilen] = '\0';
	int64_t n = (int64_t)fread(buffer, 1, ilen, fp);
	fclose(fp);
	if(olen) *olen = n;
	return buffer;
}//END: fileRead



/*
 * Объединяет корневую папку и запрошенный путь
 */
string_s *
pathConcat(string_s * root, string_s * path){
	string_s * result = (string_s *)mNew(sizeof(string_s));
	result->len = root->len + path->len;
	result->ptr = mNew(result->len + 1);
	char * ptr = result->ptr;
	ptr +=	stringCopyN(ptr, root->ptr, root->len);
			stringCopyN(ptr, path->ptr, path->len);
	return result;
}//END: pathConcat



/*
 * Объединяет корневую папку и запрошенный путь
 */
char *
pathConcatS(const char * root, const char * path, uint32_t * olen){
	size_t root_len = strlen(root);
	size_t path_len = strlen(path);
	while(*path=='/')path++,path_len--;
	while(root[root_len-1]=='/')root_len--;
	char * result = mNew(root_len + path_len + 2);
	char * ptr = result;
	ptr += 	stringCopyN(ptr, root, root_len);
	*ptr++ = '/';
	stringCopyN(ptr, path, path_len);
	if(olen) *olen = root_len + path_len + 1;
	return result;
}//END: pathConcatS



/*
 * Вычисляет и возвращает ETag на основании информации о файле struct stat
 * ETag:"9a190300-8c000000-b8b29254"
 */
string_s *
eTag(struct stat * st){
	int i;
	u_char b[17];
	b[0]=b[15]='"';
	b[16]='\0';
	b[5]=b[10]='-';
	string_s * s = (string_s *)mNewZ(sizeof(string_s));
	s->ptr = mNew(32);
	s->len = 28;
	char * ptr = s->ptr;
	*(uint32_t*)&b[1]	= (uint32_t)st->st_ino;
	*(uint32_t*)&b[6]	= (uint32_t)st->st_size;
	*(uint32_t*)&b[11]	= (uint32_t)st->st_mtime;
	for(i=0;i<16;i++){
		if(b[i]=='"'||b[i]=='-'){
			*ptr++ = b[i];
		}else{
			*ptr++ = hexTable[b[i] >> 4];
			*ptr++ = hexTable[b[i] & 0xf];
		}
	}
	*ptr = '\0';
	return s;
}//END: eTag




/***********************************************************************
 * Работа с IP адресами
 **********************************************************************/


/*
 * Преобразовывает IP адрес из  структуры socket_addr_s в текстовое представление
 */
char *
ipToString(socket_addr_s * sa, char * buf){
	size_t len = ( sa->plain.sa_family == AF_INET6 ? IPV6_LEN : IPV4_LEN );
	if(!buf) buf = mNew(len+1);
	inet_ntop(
		sa->plain.sa_family,
		sa->plain.sa_family == AF_INET6 ? (const void *) &(sa->ipv6.sin6_addr) : (const void *) &(sa->ipv4.sin_addr),
		buf, len
	);
	return buf;
}//END: ipToString




/*
 * Преобразовывает текстовое представление IP адреса в структуруы socket_addr_s
 */
socket_addr_s *
stringToIp(const char * buf, socket_addr_s * sa){
	int family = (*buf == '[' || strchr(buf,':') ? AF_INET6 : AF_INET);
	unsigned char ipaddr[IPV6_SIZE];
	if(inet_pton(family, buf, ipaddr) < 1) return NULL;
	if(!sa) sa = (socket_addr_s *) mNew(sizeof(socket_addr_s));
	sa->plain.sa_family = family;
	if(family == AF_INET6){
		memcpy(&(sa->ipv6.sin6_addr), ipaddr, IPV6_SIZE);
	}else{
		memcpy(&(sa->ipv4.sin_addr), ipaddr, IPV4_SIZE);
	}
	return sa;
}//END: stringToIp



/*
 * Сравнивает два IP адреса и возвращает true если они идентичны
 */
bool
ipCompare(socket_addr_s * ip1, socket_addr_s * ip2){
	if(!ip1 || !ip2) return false;
	if(ip1->plain.sa_family == ip2->plain.sa_family){
		if(ip1->plain.sa_family == AF_INET6){
			return (memcmp(&ip1->ipv6.sin6_addr, &ip2->ipv6.sin6_addr, sizeof(ip1->ipv6.sin6_addr)) == 0 ? true : false);
		}else{
			return (memcmp(&ip1->ipv4.sin_addr, &ip2->ipv4.sin_addr, sizeof(ip1->ipv4.sin_addr)) == 0 ? true : false);
		}
	}
	return false;
}//END: ipCompare




/***********************************************************************
 * Проверка корректности данных
 **********************************************************************/

/*
 * Проверяет корректность адреса электронной почты
 */
bool
isValidEmail(const char * email){
	int        count = 0;
	const char * c, * domain;
	static char * rfc822_specials = "()<>@,;:\\\"[]";

	//Проверка имени в email (имя@домен)
	for(c = email;  *c;  c++){
		if (*c == '\"' && (c == email || *(c - 1) == '.' || *(c - 1) ==  '\"')){
			while(*++c) {
				if(*c == '\"') break;
				if(*c == '\\' && (*++c == ' ')) continue;
				if(*c <= ' ' || *c >= 127) return false;
			}
			if(!*c++) return false;
			if(*c == '@') break;
			if(*c != '.') return false;
			continue;
		}
		if(*c == '@') break;
		if(*c <= ' ' || *c >= 127) return false;
		if(strchr(rfc822_specials, *c)) return false;
	}
	if(c == email || *(c - 1) == '.') return false;

	//Проверка домена в email (имя@домен)
	if(!*(domain = ++c)) return false;
	do{
		if(*c == '.') {
			if (c == domain || *(c - 1) == '.') return false;
			count++;
		}
		if(*c <= ' ' || *c >= 127) return false;
		if(strchr(rfc822_specials, *c)) return false;
	} while(*++c);

	return (count >= 1 ? true : false);
}//END: isValidEmail


