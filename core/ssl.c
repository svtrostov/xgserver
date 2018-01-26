 /***********************************************************************
 * XG SERVER
 * Функции работы с Open SSL 
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/ 


#include "core.h"		//Ядро
#include "server.h"


//DH параметры
static DH * dh512	= NULL;
static DH * dh1024	= NULL;
static DH * dh2048	= NULL;


//Массив мьютексов для OpenSSL
static pthread_mutex_t * mutex_array = NULL;


/***********************************************************************
 * Callback функции
 **********************************************************************/ 

/*
 * Информация о ходе "рукопожатия"
 * Параметр where представляет собой битовую маску, определяющую контекст в котором
 * вызывана функция. Параметр ret имеет положительное значение, если операция происходит
 * нормально и меньше или равен 0, если произошла ошибка.
 * Определены следующие константы, которые могут быть использованы в аргументе where:
 * SSL_CB_LOOP — Функция вызвана для того чтобы оповестить приложение об изменении состояния внутри цикла.
 * SSL_CB_EXIT — Индицирует выход из handshake в результте ошибки
 * SSL_CB_READ — Функция вызвана в процессе операции чтения
 * SSL_CB_WRITE — Функция вызвана в процессе операции записи
 * SSL_CB_ALERT — Функция вызвана так как получено внеполосное сообщение (alert) TLS-протокола.
 * SSL_CB_HANDSHAKE_START — Начало нового handshake
 * SSL_CB_HANDSHAKE_DONE — handshake завершен.
 * SSL_ST_ACCEPT — Соединение в состоянии приема клиентского соединения
 * SSL_ST_CONNECT — Соединение в состоянии установления соединения с сервером
 */
static void 
sslInfoCallback(const SSL *ssl, int where, int ret){
	if((where & SSL_CB_HANDSHAKE_START)!=0) {
		connection_s * con = SSL_get_app_data(ssl);
		con->renegotiations++;
	}
}//END: sslInfoCallback



/*
 * Callback функция получения пароля для закрытого ключа
 */
static int 
sslPasswordCallback(char * buf, int size, int rwflag, void * v_srv) {
	server_s * srv = (server_s *)v_srv;
	strncpy(buf, srv->config.private_key_password, size);
	size = strlen(buf);
	buf[size] = '\0';
	return size;
}//END: sslPasswordCallback



/*
 * Callback функция получения параметров для генерации временных ключей
 */
static DH * 
sslDHCallback(SSL *ssl, int is_export, int keylength){
	DH * tmp_dh;
	switch (keylength){
		case 512: tmp_dh = dh512; break;
		case 1024: tmp_dh = dh1024; break;
		case 2048: tmp_dh = dh2048; break;
		default:
			tmp_dh = dh1024;
		break;
	}
	return tmp_dh;
}//END: sslDHCallback



/*
 * Callback функция получения ID потока
 */
static unsigned long 
sslThreadIdCallback(void){
	return ((unsigned long)pthread_self());
}//END: sslThreadIdCallback



/*
 * Callback функция снятия / установки мьютес блокировок
 */
static void
sslThreadLockingCallback(int mode, int n, const char * file, int line){
	if (mode & CRYPTO_LOCK)
		pthread_mutex_lock(&mutex_array[n]);
	else
		pthread_mutex_unlock(&mutex_array[n]);
}//END: sslThreadLockingCallback



/*
 * Callback функция создания динамической блокировки 
 */
static struct CRYPTO_dynlock_value * 
sslDynlockCreateCallback(const char *file, int line){
	struct CRYPTO_dynlock_value * value;
	value = (struct CRYPTO_dynlock_value *) mNew(sizeof(struct CRYPTO_dynlock_value));
	if (!value) return NULL;
	pthread_mutex_init(&(value->mutex), NULL);
	return value;
}//END: sslDynlockCreateCallback



/*
 * Callback функция снятия / установки динамической блокировки
 */
static void 
sslDynlockLockingCallback(int mode, struct CRYPTO_dynlock_value *l, const char *file, int line){
	if(mode & CRYPTO_LOCK)
		pthread_mutex_lock(&(l->mutex));
	else
		pthread_mutex_unlock(&(l->mutex));
}//END: sslDynlockLockingCallback



/*
 * Callback функция уничтожения динамической блокировки
 */
static void 
sslDynlockDestroyCallback(struct CRYPTO_dynlock_value *l, const char *file, int line){
	pthread_mutex_destroy(&(l->mutex));
	free(l);
}//END: sslDynlockDestroyCallback






/***********************************************************************
 * Основные SSL функции
 **********************************************************************/ 



/*
 * Инициализация OpenSSL
 */
int
sslInit(server_s * srv){

	if(!sslThreadSetup() || !SSL_library_init()){
		FATAL_ERROR("OpenSSL initialization failed!");
		return -1;
	}
	SSL_load_error_strings();
	RAND_load_file("/dev/urandom", 1024); // Seed the PRNG
	OpenSSL_add_all_algorithms();

	//Инициализация DH параметров
	dh512 = sslDHLoad(srv->config.dh512_file);
	dh1024 = sslDHLoad(srv->config.dh1024_file);
	dh2048 = sslDHLoad(srv->config.dh2048_file);

	//Создание экземпляра SSL контекста
	sslCtxInit(srv);

	return 0;
}//END: sslInit




/*
 * Закрытие OpenSSL
 */
int
sslCleanup(void){

	if(!sslThreadCleanup()) return -1;

	return 0;
}//END: sslCleanup




/*
 * Создание экземпляра SSL контекста
 */
SSL_CTX *
sslCtxInit(server_s * srv){

	SSL_CTX *ctx = NULL;

	ctx = SSL_CTX_new(SSLv23_server_method());
	if (NULL == ctx) FATAL_ERROR("SSL_CTX_new fail");

	srv->ctx = ctx;

	sslLoadCertificates(srv);

	//Проверка клиента
	SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

	//Опции контекста: SSL_OP_ALL (обход ошибок) + SSL_OP_SINGLE_DH_USE (Создавать новый ключ из DH параметров)
	//+SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION (При повторном хэндшейке всегда создавать новую сессию)
	SSL_CTX_set_options(ctx, SSL_OP_ALL | SSL_OP_SINGLE_DH_USE | SSL_OP_NO_SSLv2 | SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION);

	//Информация о ходе "рукопожатия" 
	SSL_CTX_set_info_callback(ctx, sslInfoCallback);

	//Функция DH параметров для временных ключей
	SSL_CTX_set_tmp_dh_callback(ctx, sslDHCallback);

	//Установка кеша сессий
	SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);

	SSL_CTX_set_default_read_ahead(ctx, 1);

	//Позволяет при повторном вызове SSL_write передать буфер с тем же содержимым, расположенный в другом месте памяти
	SSL_CTX_set_mode(ctx, SSL_CTX_get_mode(ctx) | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

	return ctx;
}//END: sslInitCtx



/*
 * Загрузка сертификатов
 */
void
sslLoadCertificates(server_s * srv){

	//Функция проверки пароля закрытого кюча
	SSL_CTX_set_default_passwd_cb(srv->ctx, sslPasswordCallback);
	SSL_CTX_set_default_passwd_cb_userdata(srv->ctx, (void *)srv);


	//Загрузка файла сертификата
	if(SSL_CTX_use_certificate_file(srv->ctx, srv->config.certificate_file, SSL_FILETYPE_PEM) <= 0) {
		FATAL_ERROR("Couldn't load certificate file: %s", srv->config.certificate_file);
	}

	//Загрузка закрытого ключа
	if(SSL_CTX_use_PrivateKey_file(srv->ctx, srv->config.private_key_file, SSL_FILETYPE_PEM) <= 0) {
		FATAL_ERROR("Could not load private key file: %s", srv->config.private_key_file);
	}

	//Проверка верности закрытого ключа и сертификата
	if(!SSL_CTX_check_private_key(srv->ctx)) {
		FATAL_ERROR("Private Key and Certificate do NOT match");
	}

	return;
}//END: sslLoadCertificates



/*
 * Загрузка DH параметров
 */
DH * 
sslDHLoad(const char * dh_file){
	DH * result;
	BIO * bio;
	bio = BIO_new_file(dh_file, "r");
	if(!bio) FATAL_ERROR("Error opening DH file: %s", dh_file);
	result = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
	if(!result) FATAL_ERROR("Error reading DH parameters from: %s", dh_file);
	BIO_free(bio);
	return result;
}//END: sslDHLoad



/*
 * Инициализация потоков SSL
 */
bool
sslThreadSetup(void){

	int i;
	//CRYPTO_num_locks() - возвращает необходимое OpenSSL количество блокировок
	mutex_array = (pthread_mutex_t *) mNew( CRYPTO_num_locks() * sizeof(pthread_mutex_t) );
	if(!mutex_array) return false;

	for(i = 0; i < CRYPTO_num_locks(); i++) pthread_mutex_init(&(mutex_array[i]), NULL);

	CRYPTO_set_id_callback(sslThreadIdCallback);	//ID потока
	CRYPTO_set_locking_callback(sslThreadLockingCallback);	//Установка / снятие мьютекс блокировок

	CRYPTO_set_dynlock_create_callback(sslDynlockCreateCallback);	//Создание динамической блокировки OpenSSL
	CRYPTO_set_dynlock_lock_callback(sslDynlockLockingCallback);	//Установка / снятие динамической блокировки OpenSSL
	CRYPTO_set_dynlock_destroy_callback(sslDynlockDestroyCallback);	//Удаление динамической блокировки OpenSSL

	return true;
}//END: sslThreadSetup




/*
 * Уничтожение потоков SSL
 */
bool 
sslThreadCleanup(void){

	int i;
	if (!mutex_array) return false;

	CRYPTO_set_id_callback(NULL);
	CRYPTO_set_locking_callback(NULL);
	CRYPTO_set_dynlock_create_callback(NULL);
	CRYPTO_set_dynlock_lock_callback(NULL);
	CRYPTO_set_dynlock_destroy_callback(NULL);

	for (i = 0; i < CRYPTO_num_locks(); i++) pthread_mutex_destroy(&(mutex_array[i]));
	free(mutex_array);
	mutex_array = NULL;

	return true;
}//END: sslThreadCleanup




/*
 * Проверка готовности SSL соединения
 */
result_e
sslDoHandshake(SSL * ssl){

	int rc, error;

	if ((rc = SSL_do_handshake(ssl)) <= 0){
		error = errno;
		int err = SSL_get_error(ssl, rc);
		switch(err){
			case SSL_ERROR_NONE: return RESULT_AGAIN;
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE: 
				return RESULT_AGAIN;
			case SSL_ERROR_SYSCALL:
				switch (error) {
					case EAGAIN:
					case EINTR: return RESULT_AGAIN;
					case EPIPE:
					case ECONNRESET:
					default: return RESULT_ERROR;
				}
			default:
				return RESULT_ERROR;
			break;
		}
	}
	return RESULT_OK;
}//END: sslDoHandshakePrivate




/*
 * Чтение данных из SSL
 */
result_e
sslRead(SSL * ssl, char * buf, size_t buf_size, size_t * pcnt_read){

	int		re;
	int		error;
	ssize_t	len;

	if(pcnt_read) *pcnt_read = 0;

	len = SSL_read(ssl, buf, buf_size);
	if (len > 0){
		if(pcnt_read) *pcnt_read = len;
		if(SSL_pending(ssl)) return RESULT_AGAIN;
		return RESULT_OK;
	}

	if (len == 0){
		return RESULT_EOF;
	}

	error = errno;
	re = SSL_get_error(ssl, len);

	switch (re) {
		case SSL_ERROR_NONE: return RESULT_AGAIN;
		case SSL_ERROR_WANT_READ:
		case SSL_ERROR_WANT_WRITE: return RESULT_AGAIN;
		case SSL_ERROR_ZERO_RETURN: return RESULT_EOF;
		case SSL_ERROR_SSL: return RESULT_ERROR;
		case SSL_ERROR_SYSCALL:
			switch (error) {
				case EAGAIN:
				case EINTR: return RESULT_AGAIN;
				case EPIPE:
				case ECONNRESET: return RESULT_CONRESET;
				default: return RESULT_ERROR;
			}
		return RESULT_ERROR;
	}

	return RESULT_ERROR;
}//END: sslRead




/*
 * Запись данных в SSL
 */
result_e
sslWrite(SSL * ssl, const char * buf, size_t buf_size, size_t * pcnt_write){

	int		re;
	int		error;
	ssize_t	len;

	if(pcnt_write) *pcnt_write = 0;
	if(!buf_size) return RESULT_EOF;

	len = SSL_write(ssl, buf, buf_size);
	if (len > 0){
		if(pcnt_write) *pcnt_write = len;
		return RESULT_OK;
	}

	error = errno;
	re = SSL_get_error(ssl, len);

	switch (re) {
		case SSL_ERROR_NONE: return RESULT_AGAIN;
		case SSL_ERROR_WANT_READ:
		case SSL_ERROR_WANT_WRITE: return RESULT_AGAIN;
		case SSL_ERROR_SYSCALL:
			switch (error) {
				case EAGAIN:
				case EINTR: return RESULT_AGAIN;
				case EPIPE:
				case ECONNRESET: return RESULT_CONRESET;
				default: return RESULT_ERROR;
			}
		return RESULT_ERROR;
		case SSL_ERROR_SSL: 
		default: 
		return RESULT_ERROR;
	}

	return RESULT_ERROR;
}//END: sslWritePrivate

