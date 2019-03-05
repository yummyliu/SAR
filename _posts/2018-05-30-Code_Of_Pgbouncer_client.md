---
layout: post
title: Pgboucner源码——C/S CallBack
subtitle: 这两个模块主要是用来处理Pgbouncer前后交互的消息，其中定义了一些回调函数，不同的条件下触发相应的handler；
date: 2018-06-09 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
    - Pgbouncer
---

###### Client

模块对外提供三个接口：

1. `bool set_pool(PgSocket *client, const char *dbname, const char *username, const char *password, bool takeover)`：在pgbouncer发生takeover的时候，配置client对应的pool的相关参数
2. `bool handle_auth_response(PgSocket *client, PktHdr *pkt) `：client登录的时候，处理PostgreSQL返回的认证消息，其实是处理的server端的请求，感觉应该放在server模块中
3. `bool client_proto(SBuf *sbuf, SBufEvent evtype, struct MBuf *data)`：处理Client请求的回调函数

client的建立连接，认证以及之后的查询解析PostgreSQL的前段协议来处理查询请求进行处理；

1. client的请求由sbuf接到，调用sbuf上注册的回调函数：client_proto；client_proto通过SBufEvent调用相应的处理函数；比如handle_client_startup;或者handle_client_work;

2. handle_client_startup建立连接；建立连接；发起认证；

3. handle_client_work;处理已经登录的client的请求；（解析pg_front protocol）;一般就是直接向前转发就行了，只是更新一下pgbouncer的统计信息；以及调用find_server找到一个空闲的server然后向前转发；否则就进去client_waiting队列；

###### Server

Server模块对外提供了4个接口：

1. `int pool_pool_mode(PgPool *pool)`：返回当前pool的模式：session、transaction、statement（和这个模块的定义不太相符啊）；
2. `int database_max_connections(PgDatabase *db)`：db的最大连接数；(和这个模块的定义不太相符啊)
3. `int user_max_connections(PgUser *user)`：用户的最大连接数；(和这个模块的定义不太相符啊)
4. `bool server_proto(SBuf *sbuf, SBufEvent evtype, struct MBuf *data)`：server_proto的回调

