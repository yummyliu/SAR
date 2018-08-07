---
layout: post
title: 总结完成patrol插件中踩过的坑（TODO）
date: 2018-08-07 14:09
categories: jekyll update
tags:
  - PostgreSQL
  - C++
---

> 最近想折腾一下PostgreSQL extension，毕竟PostgreSQL正是由于其强大的可扩展性，变得越来越牛逼；我想做一个巡检的插件——patrol；其中需要使用C++的库，所以采用C/C++混编的方式实现PostgreSQL extension，分为两部分commander/patrol;
>
> Commander: PostgreSQL的一个background worker，维护巡检的信息表，以及与patrol通信获取数据；
>
> Patrol：go实现的agent，commander是client，向这些agent服务请求巡检信息。
>
> 目前各个环节初步调通，遇到不少的坑，整理一下。

## 搭建一个PostgreSQL的开发环境

先装PostgreSQL，后还有各种dev的包。。。特麻烦

#### 解决：Docker

先做了个 [postgresql-dev-docker](https://github.com/yummyliu/postgresql-dev) 的基础镜像，然后基于这个基础镜像编写commander的Dockerfile

```
FROM postgresql-dev

ADD . /commander

RUN apk add libstdc++ curl-dev jsoncpp-dev && \
	chmod +x /usr/local/bin/gosu && \
    rm -rf /docker-entrypoint-initdb.d && \
    mkdir /docker-entrypoint-initdb.d && \
    apk del curl && \
    rm -rf /var/cache/apk/*

ENV LANG en_US.utf8
ENV PGDATA /var/lib/postgresql/data
VOLUME /var/lib/postgresql/data

COPY docker-entrypoint.sh /

ENTRYPOINT ["/docker-entrypoint.sh"]

EXPOSE 5433
CMD ["postgres"]
```

写完了之后，docker build、docker run即可；意外收获，用了docker后，PostgreSQL不在电脑上跑，电脑再也不发热了！

## 结合PGXS，混编C、C++的Makefile

明白PGXS各个参数的意义，按个凑吧，不过凑的过程还挺漫长，例子如下：

```makefile
MODULE_big = commander

EXTENSION = commander
DATA = commander--1.0.sql

CXX = c++ -std=c++11 -I/usr/local/include -I/usr/local/opt/openssl/include

#CFLAGS_SL += -stdlib=libc++ -lcpprest -lssl -lcrypto -lboost_system -lboost_thread-mt -lboost_chrono-mt
CFLAGS_SL +=  -fPIC
PG_CPPFLAGS +=  -fPIC
SHLIB_LINK += -lstdc++ -lcurl -ljsoncpp
#PG_CPPFLAGS += -std=c++11
OBJS +=  command/command.o command/httpclient.o commander.o

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
```

##  invalid memory alloc

```
2018-08-07 16:57:52.436 CST [52842] ERROR:  invalid memory alloc request size 18446744072367376384
2018-08-07 16:57:52.436 CST [52842] STATEMENT:  select getjson();
INFO:  msg: [{"id":1,"maxage":19234,"name":"postgres"},{"id":2,"maxage":1239082,"name":"pgbench"}]
ERROR:  invalid memory alloc request size 18446744072367376384
```

但是其中遇到了上述问题；获取到的json结果，在如下函数返回时，报错如上的错误。

```c
PG_RETURN_CSTRING(output.c_str() );
```

#### 解决：pstrdup

```
PG_RETURN_CSTRING(pstrdup(output.c_str()));
```

## 字符串截断

```
postgres=# select getjson();
INFO:  msg: [{"id":1,"maxage":19234,"name":"postgres"},{"id":2,"maxage":1239082,"name":"pgbench"}]

                   getjson
----------------------------------------------
 {"id":1,"maxage":19234,"name":"postgres"},{"
(1 row)
```

#### 解决：类型不对

```sql
CREATE OR REPLACE FUNCTION getjson () 
    RETURNS cstring
AS 'commander.so',
'getjson'
LANGUAGE C ;
```





（未完待续）