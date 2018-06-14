---
layout: post
title: Pgbouncer内部对象的管理
subtitle: Pgbouncer用到的数据结构
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

Pgbouncer使用<db,user>可以唯一确定一个pool；每个pguser对应一个pool set `poolForUser_n`；每个pgdatabase对应一个pool set `poolForDatabase_m`；

`poolForuser_n ∩ poolForDatabase_m` 这个交集，就是某一个<db-user> 对应的pool；

objects模块，提供了若干函数，对pgbouncer的objects进行增删改查

+ **增**加某些对象

  + `PgDatabase * add_database(const char *name) _MUSTCHECK;`

    upsert 一个新的pgDatabase对象，放到相应容器中，并返回

  + `PgDatabase *register_auto_database(const char *name);`

    pgbouncer的配置文件中，可以用key=value的形式直接配置一个db，这种方式是autodb，如果相应的server一直idle，在autodb_idle_timeout之后就会被清理；

  + `PgUser * add_user(const char *name, const char *passwd) _MUSTCHECK;`

    往user_cache中，添加user

  + `PgUser * add_db_user(PgDatabase *db, const char *name, const char *passwd) _MUSTCHECK;`

    往db->user_tree中，添加user，在db中查询过的user

  + `PgUser * force_user(PgDatabase *db, const char *username, const char *passwd) _MUSTCHECK;`

    关于forced_user的意义，如果pgbouncer的database中，使用连接串的方式并且连接串中指定了user，那么pgbouncer和PostgreSQL之间的连接强制使用这个user，不管前段的用户是什么；

  + `PgUser * add_pam_user(const char *name, const char *passwd) _MUSTCHECK;`

    pam认证方式

+ **删**除某些对象

  + `evict_connection`和`evict_user_connection`

    清理poolForDatabase_m和poolForUser_n中的连接

  + `disconnect_server` 和 `disconnect_client`

    释放连接

  + `objects_cleanup`

    只是在main最后的cleanup中调用了，在生产环境中几乎没用


+ **改**动某些对象信息

  + `PgSocket *accept_client(int sock, bool is_unix) _MUSTCHECK;`

    接受client连接

  + `bool release_server(PgSocket *server)` 和 `void activate_client(PgSocket *client);`

    释放server的连接，连接的状态从connecting/active -> idle, 可能的话会和client端解除连接 ，此时会及时的调用`activate_client` 唤醒一个client；

  + `void forward_cancel_request(PgSocket *server);`

    server cancel 这次请求

  + `void accept_cancel_request(PgSocket *req);`

    client 接收到了 cancel的response，并处理

  + `use_client_socket`和`use_server_socket`

    takeover的时候，复用Socket

  + `change_client_state` 和 `change_server_state`

    改变状态，意味着改变相应的list，这两个函数就是管理状态和list的对应

  + `void tag_database_dirty(PgDatabase *db);`

    db上的查询发生改变，

  + `void tag_autodb_dirty(void);` 

    autodb 的链接串发生改变

  + `void tag_host_addr_dirty(const char *host, const struct sockaddr *sa);`

    链接上的请求结果发生改变

  +  `void reuse_just_freed_objects(void);`

    object从just_freed到freedlist中

  + `init_objects` 和`init_caches`

    加载配置之前，初始化对象



+ **查**找某些对象的信息


  + `find_database` `find_user`

    从相应list中，找到odject

  + `PgPool *get_pool(PgDatabase *, PgUser *);`

    按照db-user对，唯一标识一个pool；

  + `PgSocket *compare_connections_by_time(PgSocket *lhs, PgSocket *rhs);`

    返回rhs lhs中，请求时间老的连接

  + `bool find_server(PgSocket *client)		_MUSTCHECK;`

    client请求server连接，如果找到了，那么两个socket建立连接关系，否则，放在等待队列里

  + `bool finish_client_login(PgSocket *client)	_MUSTCHECK;`

    改变client的状态，模拟server发送一些欢迎语

  + `bool check_fast_fail(PgSocket *client)		_MUSTCHECK;`

  + `void launch_new_connection(PgPool *pool);`

    新的client连接请求，快速检查client对应的pool中有没有可用的server连接，没有的话，不放在等待队列中，只是触发一次新的launch_new_connection，pool申请新的连接

  + `int get_active_client_count(void);`和`int get_active_server_count(void);`

    字面意思，都是从slab这个带统计信息的双向链表中取count

+ 其他

  ```c
  // for_each
  void for_each_server(PgPool *pool, void (*func)(PgSocket *sk));
  ```

  ​