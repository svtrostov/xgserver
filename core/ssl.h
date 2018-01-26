 /***********************************************************************
 * XG SERVER
 * Функции работы с Open SSL 
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/


#include <pthread.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/x509v3.h>
#include <openssl/rand.h>
#include <openssl/bio.h>

#define XG_SSL_REPEATER 20U

#define CLEAR_LIBSSL_ERRORS \
	do { \
		unsigned long openssl_error; \
		while ((openssl_error = ERR_get_error())) { \
		} \
	} while(0)

/*Структура CRYPTO_dynlock_value, согласно требованиям OpenSSL, должна быть определна самоcтоятельно*/
struct CRYPTO_dynlock_value{
	pthread_mutex_t mutex;
};


int			xg_ssl_init(void);	//Инициализация SSL
int			xg_ssl_cleanup(void);	//Закрытие OpenSSL
SSL_CTX *	xg_ssl_init_ctx(void);	//оздание экземпляра SSL контекста
int			xg_ssl_load_certificates(SSL_CTX *ctx);	//Загрузка сертификатов
DH *		xg_ssl_dh_load(const char * dh_file);	//Загрузка DH параметров
void		xg_ssl_dh_init(void);	//Инициализация и загрузка DH параметров
bool		xg_ssl_thread_setup(void);	//Инициализация потоков SSL
bool 		xg_ssl_thread_cleanup(void);	//Уничтожение потоков SSL
bool		xg_ssl_do_handshake(SSL * ssl);	//Установка SSL соединения (рукопожатие)
bool		xg_ssl_check_accept(SSL * ssl);	//Проверка готовности SSL соединения
void 		xg_ssl_show_cert(SSL* ssl);	//Вывод сертификата клиента
result_e	xg_ssl_read(SSL * ssl, char * buf, size_t buf_size, size_t * pcnt_read);	//Чтение данных из SSL
bool		xg_ssl_write(SSL * ssl, const char * buf, size_t buf_size);	//Запись данных в SSL
