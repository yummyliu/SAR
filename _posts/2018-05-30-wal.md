---
layout: post
title: 
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PG
---

# WAL

## Overview

PG中将所有的改动，作为历史数据，写入到持久存储中，历史数据就是WAL；

当有change操作的时候，在wal buffer中写一个XLOGrecord，当一个事务commit或者abort的时候，这些操作就立马写入到wal file中。LSN（log sequence number）代表xlog的位置。record的lsn就是xlog的唯一标识；

#### wal记录

当我们做db recovery的时候，有一个问题：PG从哪个点开始恢复的？就是REDOpoint，即，最近的checkpoint开始的时候写入的xlog record。

1. checkpointer后台进程，间歇性的执行checkpoint。当checkpointer启动，它在current wal seg中写入一个checkpoint record，这条记录记录的最新的redo point；
2. insert/delete/update。。。。的时候，将相应操作写成一个xlog record，写入wal buffer中，并更新table page的header的lsn字段。
3. 当事务commit的时候，将commit语句写一个xlog record写入到 wal buffer中，并刷新到wal file中。
4. 当crash的时候，所有的share buffer中的更改丢失，但是wal中有所有commit的history data

##### 恢复

1. PostgreSQL的每个page的header上有最后更新的lsn，将page和xlog的record读取到内存中。
2. PostgreSQL比较xlog record的lsn和table page的lsn：如果xlog record比较大，那么应用这个record，并修改page的lsn；否则，不做操作
3. 将左右的xlog record都重放一下；

#### Full-page Write

当磁盘上的table page data损坏，这样bgwriter写数据的时候，os失败了。因为wal不能重放损坏页，这就有问题；

但是PostgreSQL提供了一个特性：full-page writes；默认打开，打开的话：**在每次checkpoint之后**，xlog record会记录page header-data和整个page的信息到record中。这样包含整个page的record称为 backup block( full-page image)。如此wal的记录过程如下：

1. checkpointer 启动一个进程
2. 某一个页上发生修改，PostgreSQL将整个page，写入到record中；
3. 后续改页上的修改，不写整个页了（该特性主要防止bgwriter刷checkpoint的时候没有完全刷成功，留一个后手）

##### 有full-page image的恢复：

1. PostgreSQL将xlog record和page读取到shard buffer中。
2. 当record时full-page image时，直接用full-page image覆盖这个page，并更新这个page的lsn；
3. 后续的非full-page的record的处理，和之前一样；

## Transaction log & wal segment files

事务日志可以非常大，但是如果在一个文件里不方便管理，我们将事务日志切分为16Mb的segment；每个segment是一个24位的16进制数字，前八位是timelineid，中间8位和最后8位可以看做是 一个256进制的两位数：

当最后8位满0xFF进1的时候，中间8位+1；一次类推

> 通过pg_walfile_name函数，可以传入一个lsn值，得到该lsn对应的record在哪个wal中；
>
> ```sql
> testdb=# SELECT pg_xlogfile_name('1/00002D3E');  # In version 10 or later, "SELECT pg_walfile_name('1/00002D3E');"
>      pg_xlogfile_name     
> --------------------------
>  000000010000000100000000
> ```

## Internal layout of wal segment

![wal](/image/fig-9-07.png)

一个16Mb的wal段，内部切分为8k大小的page；第一个page有一个header-data(XLogLongPageHeaderData)其他的page的header定义为XLogPageHeaderData; 基于header的定义每个xlog record记录相应的record;

## Internal Layout of XLOG Record

TODO

## Write of XLOG Records

当执行了如下一个语句：

```sql
testdb=# INSERT INTO tbl VALUES ('A');
```

内部函数exec_simple_query被执行：

```c
exec_simple_query() @postgres.c

(1) ExtendCLOG() @clog.c                  /* 在CLOG中写入该事务的信息：
                                           * "IN_PROGRESS" .
                                           */
(2) heap_insert()@heapam.c                /* 插入一条tuple，创建一个xlog record,
                                           * 调用XLogInsert函数.
                                           */
(3)   XLogInsert() @xlog.c (9.5 or later, xloginsert.c)
                                          /* 把xlog record写入到 WAL buffer中，并更新page的											* pd_lsn.
                                           */
(4) finish_xact_command() @postgres.c     /* commit*/   
      XLogInsert() @xlog.c  (9.5 or later, xloginsert.c)
                                          /* Write a XLOG record of this commit action 
                                           * to the WAL buffer.
                                           */
(5)   XLogWrite() @xlog.c                 /* Write and flush all XLOG records on 
                                           * the WAL buffer to WAL segment.
                                           */
(6) TransactionIdCommitTree() @transam.c  /* Change the state of this transaction 
                                           * from "IN_PROGRESS" to "COMMITTED" on the CLOG.
```

## Wal Writer Process

这个服务进程间歇的运行，将wal buffer刷新到磁盘上，主要是防止同时提交太多，导致磁盘IO过载；

## Checkpoint Processing

当以下三种情况下，checkpoint进程启动：

1. `checkpoint_timeout`： 距离上次checkpoint过去了这些时间，默认5分钟
2. 9.4之前的配置了checkpoint_segment参数，默认是3，消费了这些wal segment，就会触发checkpoint
3. 9.5之后，总的walfile超过了`max_wal_size`；默认64个file 1GB
4. PostgreSQL在smart或者fast模式stop
5. 手动执行，checkpoint

## PostgreSQL恢复

![re](/image/fig-9-14.png)

1. 当POstgreSQL启动的时候，检查pg_control中的state字段，如果该字段是 in production; PostgreSQL进入recovery-mode，因为PostgreSQL没有正常结束；如果是shut down, 那么进入startup-mode;
2. 从pg_control读取最新的checkpoint记录，从记录了checkpoint的xlog record中读取到redo point；进行恢复，如果该checkpoint损坏，从pg_control读取prior checkpoint（PG11中将不会存储这个信息）；
3. 开始恢复（non-backup block中的redo 操作不是幂等的，所以要按照xlog的lsn大小，顺序恢复）；

## Wal segment file 管理

### walseg切换case

1. 满了
2. pg_switch_xlog
3. archive_mode is one and it time archive_timeout

