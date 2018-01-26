<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
	<head>
		<title>Вход</title>
		<meta http-equiv="X-UA-Compatible" content="IE=9"/>
		<meta charset="UTF-8">
		<link rel="stylesheet" type="text/css" href="/css/ui-reset.css"/>
		<link rel="stylesheet" type="text/css" href="/css/ui-login.css"/>
		<script type="text/javascript">
			var COOKIE_PREFIX = "CASCADE";
			var INTERFACE_LANG = "ru";
			var INTERFACE_THEME = "default";
			var INTERFACE_MODULE = "Main";
			var APP_AUTOCREATE_DISABLE = true;
		</script>
		
	</head>
	<body>
	
		<div class="login_area">
			<div class="logo">{[@lang:/users/login/form/title]}</div>
			<div class="login_error" style="display:{[edisplay]};"><h3>{[emessage]}</h3></div>
			<div class="loginbox">
				<form action="/" method="post">
					<input type="hidden" name="page" value="" />
					<input type="hidden" name="logintype" value="login" />
					<div class="username_field"><input type="text" name="login" placeholder="{[@lang:/users/login/form/login]}" class="required" value="" /></div>
					<div class="password_field"><input type="password" name="password" placeholder="{[@lang:/users/login/form/password]}" class="required" value="" /></div>
					<div class="buttonline">
						<input type="submit" class="loginbutton" value="Вход"/>
					</div>
				</form>
			</div>
		
		</div>

	</body>	

</html>