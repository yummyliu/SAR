---
layout: post
title: 
date: 2018-06-13 08:07
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - PG
  - Pgbouncer
---

takeover操作在pgbouncer中，就是当pgbouncer重启的时候，接管原来的资源：Socket fd，objects；

对外主要提供4个函数：

```c
void takeover_init(void);
bool takeover_login(PgSocket *bouncer) _MUSTCHECK;
void takeover_login_failed(void);
void takeover_finish(void);
```

##### takeover_init

在pgbouncer启动的时候，会创建一个dbname为pgbouncer的的假连接池，作为admin pool；takeover初始化时：

1. 取出pgbouncer对应admin pool；没有则报错
2. admin_pool尝试添加新的连接；

```bash
2018-06-13 11:38:30.770 7656 LOG takeover_init: launching connection
2018-06-13 11:38:30.770 7656 LOG S-0x249d690: pgbouncer/pgbouncer@unix:6432 new connection to server
2018-06-13 11:38:30.770 7497 LOG C-0x18e4a30: (nodb)/(nouser)@unix(7656):6432 closing because: client unexpected eof (age=0)
2018-06-13 11:38:30.771 7497 LOG C-0x18e4a30: (nodb)/pgbouncer@unix(7656):6432 pgbouncer access from unix socket
```

##### takeover_login

takeover_init中，pool需要向PostgreSQL申请新的连接，就是一个登录请求；**PostgreSQL**认证成功返回'Z'(ReadyForQuery).

在server_proto回调中，调用`handle_server_startup`，pgbouncer模拟客户端，回复一个'Q'（Query）；而后到调用`takeover_login`。`takeover_login` 中向连接中发送了`SUSPEND`指令；同时注册一个回调`takeover_recv_cb` ，等待suspend指令结束，执行下一步；

```c
		/* let the takeover process handle it */
		if (res && server->pool->db->admin)
			res = takeover_login(server);
```

```bash
2018-06-13 11:38:30.771 7497 LOG C-0x18e4a30: pgbouncer/pgbouncer@unix(7656):6432 login attempt: db=pgbouncer user=pgbouncer tls=no
2018-06-13 11:38:30.771 7656 LOG S-0x249d690: pgbouncer/pgbouncer@unix:6432 Login OK, sending SUSPEND
2018-06-13 11:38:30.771 7497 LOG SUSPEND command issued
```

`suspend`指令结束，触发回调，若suspend成功结束：

```c
		case 'C': /* CommandComplete */
			log_debug("takeover_parse_data: 'C'");
			next_command(bouncer, &pkt.data);
			break;
```

那么，接着执行后面的命令 `show fds`，打印现在的fd信息：

```bash
2018-06-13 11:38:30.771 7656 LOG SUSPEND finished, sending SHOW FDS
2018-06-13 11:38:30.771 7656 LOG got pooler socket: 127.0.0.1:6432
2018-06-13 11:38:30.772 7656 LOG got pooler socket: unix:6432
2018-06-13 11:38:30.772 7656 LOG SHOW FDS finished
2018-06-13 11:38:30.772 7656 LOG disko over, going background
```

##### takeover_login_failed

打个日志而已，郑重其事地搞了个函数🙄

##### takeover_finish

```c
	if (cf_reboot) {
		if (check_old_process_unix()) {
			takeover_part1();
			did_takeover = true;
		} else {
			log_info("old process not found, try to continue normally");
			cf_reboot = 0;
			check_pidfile();
		}
	}

......
    
    if (did_takeover) {
		takeover_finish();
	} else {
		pooler_setup();
	}
```

重启后如果进入takeover模式，在`takeover_part1`中，将大部分的工作完成，最后`takeover_finish`进行一个收尾工作:

1. shut down old pgbouncer

2. 等待old pgbouncer的sbuf中的 shutdown的reponse信息，直到成功；

3. 关闭老的pgbouncer的连接

4. 继续原来的Socket连接

   ```bash
   2018-06-13 11:38:30.775 7658 LOG sending SHUTDOWN;
   2018-06-13 11:38:30.775 7497 LOG SHUTDOWN command issued
   2018-06-13 11:38:30.776 7658 LOG S-0x249d690: pgbouncer/pgbouncer@unix:6432 closing because: disko over (age=0)
   2018-06-13 11:38:30.776 7658 LOG waiting for old pidfile to go away
   2018-06-13 11:38:30.776 7658 LOG old process killed, resuming work
   2018-06-13 11:38:30.776 7658 LOG process up: pgbouncer 1.8.1, libevent 2.0.21-stable (epoll), adns: c-ares 1.10.0, tls: OpenSSL 1.0.1e-fips 11 Feb 2013
   ```



##### Question

这个模块和整理的关联比较大，看完还是有几点疑惑，等后续各个部分都看完，再梳理；

1. pgbouncer是一个特殊的pool，takeover中，基本就是用一个新的pgbouncer进程替代老的pgbouncer进程，这个pool中与PostgreSQL有没有连接？
2. pgbouncer如何切换的进程？



