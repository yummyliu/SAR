---
layout: post
title: 
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PG
---

### 初始化监听

```c
main
->pooler_setup
->resume_pooler
->event_set(&ls->ev, ls->fd, EV_READ | EV_PERSIST, pool_accept, ls)
```

pgbouncer启动的时候，注册一个pool_accept回调函数，监听连接请求

### 客户端连接

```c
pool_accept
->accept_client
->sbuf_accept
->sbuf_wait_for_data
->event_set(&sbuf->ev, sbuf->sock, EV_READ | EV_PERSIST, sbuf_recv_cb, sbuf);
```

连接建立后，继续监听相应fd，接收客户端请求数据

### 连接上有数据

```c
sbuf_recv_cb
->sbuf_main_loop
```

### 数据解析与转发

sbuf_main_loop:接收，解析，发送 

启动pgbouncer，使用psql连接上pgbouncer，在sbuf_process_pending打上断点；

```c
psql -U postgres -p 6432 -h 127.0.0.1
sudo tcpdump -i lo host 127.0.0.1 and tcp port 6432 or 5432
gdb attach pidofpgbouncer
```

tcpdump观察的网络包，分为三部分：

1. psql将请求发给pgbouncer

```
17:11:46.425773 IP localhost.41902 > localhost.pgbouncer: Flags [P.], seq 723658268:723658306, ack 1059626101, win 124, options [nop,nop,TS val 3580159264 ecr 3580126976], length 38
17:11:46.425787 IP localhost.pgbouncer > localhost.41902: Flags [.], ack 38, win 86, options [nop,nop,TS val 3580159264 ecr 3580159264], length 0

```

2. pgbouncer转发请求给PostgreSQL

```
17:11:49.987977 IP localhost.55014 > localhost.postgres: Flags [P.], seq 559222864:559222902, ack 33485426, win 124, options [nop,nop,TS val 3580162826 ecr 3580126208], length 38
17:11:49.988525 IP localhost.postgres > localhost.55014: Flags [P.], seq 1:195, ack 38, win 86, options [nop,nop,TS val 3580162827 ecr 3580162826], length 194
17:11:49.988534 IP localhost.55014 > localhost.postgres: Flags [.], ack 195, win 126, options [nop,nop,TS val 3580162827 ecr 3580162827], length 0
```

3. pgbouncer返回psql

```
17:11:54.270388 IP localhost.pgbouncer > localhost.41902: Flags [P.], seq 1:195, ack 38, win 86, options [nop,nop,TS val 3580167109 ecr 3580159264], length 194
17:11:54.270403 IP localhost.41902 > localhost.pgbouncer: Flags [.], ack 195, win 126, options [nop,nop,TS val 3580167109 ecr 3580167109], length 0
```

#### 数据解析

1. sbuf分配空间
2. 如果达到sbuf loop_cnt，那么就处理完剩下的数据，不再继续在该socket上接收数据
3. 接收新数据
4. process_pending
5. clean buffer

```c
// sbuf_process_pending 

```

### 定期维护

```c
do_full_maint
refresh_stats
```

### client.c

client的建立连接，认证以及之后的查询解析PostgreSQL的前段协议来处理查询请求进行处理；

1. client的请求由sbuf接到，调用sbuf上注册的回调函数：client_proto；client_proto通过SBufEvent调用相应的处理函数；比如handle_client_startup;或者handle_client_work;

2. handle_client_startup建立连接；建立连接；发起认证；

3. handle_client_work;处理已经登录的client的请求；（解析pg_front protocol）;一般就是直接向前转发就行了，只是更新一下pgbouncer的统计信息；以及调用find_server找到一个空闲的server然后向前转发；否则就进去client_waiting队列；

   ​