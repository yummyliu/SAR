---
layout: post
title: 
date: 2018-06-12 13:35
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - PG
  - Pgbouncers
---

>  本模块实现在不同的list之间，操作pgbouncer的objects

#### Objects

```c
extern struct AATree user_tree;

extern struct StatList user_list;
extern struct StatList pool_list;
extern struct StatList database_list;
extern struct StatList autodatabase_idle_list;
extern struct StatList login_client_list;

extern struct Slab *client_cache;
extern struct Slab *server_cache;
extern struct Slab *db_cache;
extern struct Slab *pool_cache;
extern struct Slab *user_cache;
extern struct Slab *iobuf_cache;
```



#### Functions

Pgbouncer使用db-user可以唯一确定一个pool；每个pguser对应一个pool集合 `poolForUser_n`；每个pgdatabase对应一个pool集合`poolForDatabase_m`；`poolForuser_n ∩ poolForDatabase_m` 就是某一个db-user 对应的pool；

```c
PgDatabase *find_database(const char *name);
PgUser *find_user(const char *name);
// 按照db-user对，唯一标识一个pool；
PgPool *get_pool(PgDatabase *, PgUser *);
// 返回rhs lhs中，请求时间老的连接
PgSocket *compare_connections_by_time(PgSocket *lhs, PgSocket *rhs);

// 清理poolForDatabase_m和poolForUser_n中的连接
bool evict_connection(PgDatabase *db)		_MUSTCHECK;
bool evict_user_connection(PgUser *user)	_MUSTCHECK;

// client请求server连接，如果找到了，那么两个socket建立连接关系，否则，放在等待队列里；
bool find_server(PgSocket *client)		_MUSTCHECK;
// 释放server的连接，连接的状态从connecting/active -> idle, 可能的话会了client端解除连接 ，此时
// 会及时的调用activate_client 唤醒一个client
bool release_server(PgSocket *server)		/* _MUSTCHECK */;
void activate_client(PgSocket *client);
// 改变client的状态，模拟server发送一些欢迎语
bool finish_client_login(PgSocket *client)	_MUSTCHECK;

// 新的client连接请求，快速检查client对应的pool中有没有可用的server连接，没有的话，不放在等待队列中，只
// 是触发一次新的launch_new_connection，pool申请新的连接
bool check_fast_fail(PgSocket *client)		_MUSTCHECK;
void launch_new_connection(PgPool *pool);

// 接受client连接
PgSocket *accept_client(int sock, bool is_unix) _MUSTCHECK;
void disconnect_server(PgSocket *server, bool notify, const char *reason, ...) _PRINTF(3, 4);
void disconnect_client(PgSocket *client, bool notify, const char *reason, ...) _PRINTF(3, 4);

// upsert 一个新的pgDatabase对象，放到相应容器中，并返回
PgDatabase * add_database(const char *name) _MUSTCHECK;

// pgbouncer的配置文件中，可以用key=value的形式直接配置一个db，这种方式是autodb，如果相应的server一直idle，在autodb_idle_timeout之后就会被清理；
PgDatabase *register_auto_database(const char *name);

// 往user_cache中，添加user
PgUser * add_user(const char *name, const char *passwd) _MUSTCHECK;
// 往db->user_tree中，添加user，在db中查询过的user
PgUser * add_db_user(PgDatabase *db, const char *name, const char *passwd) _MUSTCHECK;
// 关于forced_user的意义，如果pgbouncer的database中，使用连接串的方式并且连接串中指定了user，那么pgbouncer和PostgreSQL之间的连接强制使用这个user，不管前段的用户是什么；
PgUser * force_user(PgDatabase *db, const char *username, const char *passwd) _MUSTCHECK;
// pam认证方式
PgUser * add_pam_user(const char *name, const char *passwd) _MUSTCHECK;

// server cancel 这次请求
void forward_cancel_request(PgSocket *server);
// client 接收到了 cancel的response，并处理
void accept_cancel_request(PgSocket *req);

// takeover的时候，复用client Socket
bool use_client_socket(int fd, PgAddr *addr, const char *dbname, const char *username, uint64_t ckey, int oldfd, int linkfd,
		       const char *client_end, const char *std_string, const char *datestyle, const char *timezone,
		       const char *password)
			_MUSTCHECK;
// takeover的时候，复用server Socket
bool use_server_socket(int fd, PgAddr *addr, const char *dbname, const char *username, uint64_t ckey, int oldfd, int linkfd,
		       const char *client_end, const char *std_string, const char *datestyle, const char *timezone,
		       const char *password)
			_MUSTCHECK;

// 改变状态，意味着改变相应的list，这两个函数就是管理状态和list的对应
void change_client_state(PgSocket *client, SocketState newstate);
void change_server_state(PgSocket *server, SocketState newstate);

// 字面意思，都是从slab这个带统计信息的双向链表中取count
int get_active_client_count(void);
int get_active_server_count(void);

// 相应的对象发生改变
void tag_database_dirty(PgDatabase *db); // db上的查询发生改变
void tag_autodb_dirty(void);  // autodb 的链接串发生改变
void tag_host_addr_dirty(const char *host, const struct sockaddr *sa); // 链接上的请求结果发生改变

// for_each
void for_each_server(PgPool *pool, void (*func)(PgSocket *sk));

// just_freelist -> free_list
void reuse_just_freed_objects(void);

void init_objects(void);

void init_caches(void);

void objects_cleanup(void);
```

