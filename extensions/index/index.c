/***********************************************************************
 * XGSERVER EXTENSION
 * index.c
 * Расширение: обработка index страницы
 * 
 * Copyright (с) 2014-2015 Stanislav V. Tretyakov, svtrostov@yandex.ru
 **********************************************************************/


#include "core.h"
#include "kv.h"
#include "server.h"
#include "globals.h"
#include "session.h"
#include "db.h"
#include "user.h"
#include "language.h"
#include "template.h"


static result_e 
handleIndex(connection_s * con){
	char ip[IPV6_LEN+1];
	ipToString(&con->remote_addr, ip);
	user_s * user;
	buffer_s * body;
	template_s * template;

	//Пользователь не авторизован
	if(!con->session->user_id || (user = userFromSession(con->session)) == NULL){

		//GET
		if(con->request.request_method != HTTP_POST){
			template = templateCreate("index.tpl", con->server->config.private_html.ptr);
			templateBindString(template,"edisplay",CONST_STR_COMMA_LEN("none"));
			body = templateParse(template, NULL);
			if(!body){
				con->http_code = 404;
				return RESULT_ERROR;
			}
			echoBuffer(con, body);
			templateFree(template);
			return RESULT_OK;
		}


		const char * login = requestGetGPC(con, "login","p",NULL);
		const char * password = requestGetGPC(con, "password","p",NULL);
		auth_state_e auth_state = AUTH_OK;

		if(!login || !*login || !password || !*password) auth_state = AUTH_LP_EMPTY;

		if(auth_state == AUTH_OK){
			if(userPolicyCheckLogin(login)!=RUSER_OK/*|| userPolicyCheckPassword(password)!=RUSER_OK*/){
				auth_state = AUTH_LP_INCORRECT;
			}
		}

		if(auth_state == AUTH_OK) auth_state = userLogin(con->session, login, password);

		if(auth_state != AUTH_OK){

			template = templateCreate("index.tpl", con->server->config.private_html.ptr);
			templateBindString(template,"edisplay",CONST_STR_COMMA_LEN("block"));
			templateBindString(template,"emessage",userAuthToString(auth_state, NULL), 0);
			body = templateParse(template, NULL);
			if(!body){
				con->http_code = 404;
				return RESULT_ERROR;
			}
			echoBuffer(con, body);
			templateFree(template);
			return RESULT_OK;
			//<div class="login_error"><h3>Неправильно указаны имя пользователя или пароль</h3></div>

		}//auth_state != AUTH_OK

		user = userFromSession(con->session);

		body = bufferCreate(1024);
		bufferAddStringFormat(
			body,
			"<h2>AUTH FROM LOGIN SUCCESSFULLY</h2>\r\n" \
			"<h3>About connection:</h3>\r\n"\
			"IP address: %s<br>\r\n"\
			"Server time: %g<br>\r\n"\
			"Session ID: %s<br>\r\n"\
			"User ID: %d<br>\r\n"\
			"Language: %s<br>\r\n"\
			"login: %s<br>\r\n"\
			"password: %s<br>\r\n"\
			"<br>\r\n<br>\r\n<a href='/'>Go to index page</a><br>\r\n",
			ip,
			time(NULL),
			con->session->session_id,
			(!user ? (int64_t)0 : (int64_t) user->user_id),
			(!user ? "NULL" : user->language),
			login,
			password
		);
		echoBuffer(con, body);
		sessionSetBool(con->session,"content",true);
		return RESULT_OK;
	}//Пользователь не авторизован



	body = bufferCreate(1024);
	bufferAddStringFormat(
		body,
		"<h2>AUTH FROM COOKIE</h2>\r\n" \
		"<h3>About connection:</h3>\r\n"\
		"IP address: %s<br>\r\n"\
		"Server time: %g<br>\r\n"\
		"Session ID: %s<br>\r\n"\
		"User ID: %d<br>\r\n"\
		"Language: %s<br>\r\n"\
		"<br>\r\n<br>\r\n<a href='/'>Go to index page</a><br>\r\n",
		ip,
		time(NULL),
		con->session->session_id,
		(!user ? (int64_t)0 : (int64_t) user->user_id),
		(!user ? "NULL" : user->language)
	);
	echoBuffer(con, body);
	return RESULT_OK;



	mysql_s * db = threadGetMysqlInstance("main");
	if(db){

		mysqlTemplate(db, "SELECT `name`,`example`,`url` FROM `help_topic` LIMIT 20", 0, true);
		mysqlBindInt(db, 12);
		mysqlBindInt(db, 16);

		if(mysqlQuery(db, NULL, 0) && mysqlUseResult(db)){
			MYSQL_ROW row;
			unsigned int num_fields;
			unsigned int i;

			body = bufferCreate(1024);
			bufferAddString(body,"<br><h2>MySQL DATA:</h2><br><table border=\"1\" cellpadding=\"4\" cellspacing=\"2\"><thead><th>Row #</th>");
			num_fields = db->fields_count;
			for (i = 0; i < num_fields; i++){
				bufferAddStringFormat(body, "<th>%s</th>",db->fields[i].name);
			}
			bufferAddString(body,"</thead><tbody>");

			while((row = mysqlFetchRow(db))!=NULL){
				//kv_s * kv = mysqlRowAsKV(db);
				/*
				buffer_s * b = mysqlRowAsJson(db, ROWAS_ARRAY, NULL);
				bufferPrint(b);
				bufferFree(b);
				*/
				bufferAddStringFormat(body, "<tr><td>%d</td>", (int64_t)db->row_index);
				for (i = 0; i < num_fields; i++){
					bufferAddStringFormat(body, "<td>%s (%d)</td>", db->data_row[i], (int64_t)db->data_lengths[i]);
				}

				bufferAddString(body,"</tr>");
			}
			bufferAddString(body,"</tbody></table>");
			//chunkqueueAddBuffer(con->response.content, body, 0, body->count, true);
			bufferFree(body);
		}//mysqlQuery


		sessionSetBool(con->session,"content",true);

	}else{
		DEBUG_MSG("INSTANCE [main] NOT FOUND");
	}



	return RESULT_OK;
}





/*
 * Стартовая функция расширения
 */
int 
start(extension_s * extension){
	DEBUG_MSG("Extension loaded: %s\n", extension->filename);
	routeAdd("/index", handleIndex);
	return 0;
}//END: start



