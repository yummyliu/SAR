---
layout: post
title: PostgreSQL浅见
subtitle: 作为DBA的第六个月，以PostgreSQL为例，梳理一下若干概念
date: 2018-06-14 13:46
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - DB
    - PostgreSQL
typora-root-url: ../../SAR
---

> 从一开始的仓颉造纸，到最后的磁盘上的01串，存储媒介不断发生变化；而当进入信息时代，如何有效的在计算机中管理数据，最后就是数据库管理系统的任务了；而数据库就是解决两个问题：存储和计算；这两个任务能够有效的做好，完备的监控是必须的；
>
> 本文从三个角度，来概述PostgreSQL
>
> 1. 有效的存储
>    1. 存储介质
>    2. 存储结构
>    3. 存储冗余
> 2. 高效的计算
>    1. 单个计算
>    2. 多个计算
> 3. 完备的监控
>    1. 当前的状态
>    2. 历史的状态

## *有效*的存储

> 先把数据放好

### 存储介质

###### 内存

热数据的buffer，pg_prewarm预热

###### SSD

有条件的公司，重要的业务放在SSD上，随机IO与顺序IO没太大差别；可以有针对的调整一下`random_page_cost = 1.1`

###### 磁盘

比SSD还冷的数据，放在这，比如每天的备份和归档

### 存储结构

#### 内存里的结构

##### PostgreSQL 内存划分

###### shared memory

+ shared buffer pool

  [PostgreSQL 缓存管理](http://yummyliu.github.io/jekyll/update/2018/05/31/Buffer_manager_PG/)

+ WAL buffer

  Wal日志的切换时机：

  + wal buffer满了
  + `pg_switch_xlog`
  + `archive_mode`=on & `archive_timeout`

+ Commit LOG : pg_clog文件的缓存

###### backend process

+ work_mem： 太大连接多了，占内存；太小，sort、hash计算慢；
+ temp_buffers：不用担心work_mem不够，可以降级用磁盘
+ maintenance_work_mem：vacuum这种维护进程的mem，可以比work_mem大一点

##### PostgreSQL 内存管理

###### bgwriter process

为了减少checkpoint对系统的影响，定时`bgwriter_delay`地刷一下脏页

###### checkpoint process

定时`checkpoint_timeout`的，在wal日志中，记一个[checkpoint](http://yummyliu.github.io/jekyll/update/2018/05/30/checkpoints/)记录，并刷新脏页；

1. 找到脏页
2. 写脏页（可能再文件缓存中）
3. fsync到磁盘中 (`checkpoint_completion_target`，控制何时集中fsync)

#### 磁盘里的结构

##### PostgreSQL 目录结构

>  以PG10为例

```bash
drwx------ 6 postgres postgres   4096 Jan 22 15:54 base : 每个库的数据文件
-rw------- 1 postgres postgres     30 Jun 15 00:00 current_logfiles : 当前的日志文件
drwx------ 2 postgres postgres   4096 Jun 15 05:08 global : 系统表
drwx------ 2 postgres postgres   4096 Jan 28 00:00 log ： 自己配置的日志位置
drwx------ 2 postgres postgres   4096 Jan 22 15:09 pg_commit_ts ： 时间戳
drwx------ 2 postgres postgres   4096 Jan 22 15:09 pg_dynshmem ： share mem用的文件？原理？
-rw-r--r-- 1 postgres postgres   1336 Apr 27 14:16 pg_hba.conf ： 。。。
-rw------- 1 postgres postgres   4513 Jan 22 15:09 pg_hba.conf.bak ： 、、、
-rw-r--r-- 1 postgres postgres    898 Feb  6 11:53 pg_hba.confe ： 。。。
-rw------- 1 postgres postgres   1636 Jan 22 15:09 pg_ident.conf ： OS user和DB user的map
drwx------ 4 postgres postgres   4096 Jun 15 08:59 pg_logical ： logical解码用的
drwx------ 4 postgres postgres   4096 Jan 22 15:09 pg_multixact ： 多个事务shard row lock 状态
drwx------ 2 postgres postgres   4096 Jan 30 09:57 pg_notify ： listen/notify
drwx------ 2 postgres postgres   4096 Jan 22 15:29 pg_replslot : replication slot
drwx------ 2 postgres postgres   4096 Jan 22 15:09 pg_serial ： 串行化提交的事务
drwx------ 2 postgres postgres   4096 Jan 22 15:09 pg_snapshots ： 两个事务使用同一个快照
# pg_export_snapshot()
# SET TRANSACTION SNAPSHOT snapshot_id
drwx------ 2 postgres postgres   4096 Jan 30 09:57 pg_stat ： 统计信息的文件
drwx------ 2 postgres postgres   4096 Jun 15 09:29 pg_stat_tmp ： 统计信息的临时文件
drwx------ 2 postgres postgres  20480 Jun 15 09:28 pg_subtrans ： 子事务状态
drwx------ 2 postgres postgres   4096 Jan 22 15:09 pg_tblspc ： tablespace的软连接
drwx------ 2 postgres postgres   4096 Jan 22 15:09 pg_twophase ： 两阶段提交的prepare阶段
-rw------- 1 postgres postgres      3 Jan 22 15:09 PG_VERSION ： 主版本号
drwx------ 3 postgres postgres 618496 Jun 15 08:59 pg_wal ： wal
drwx------ 2 postgres postgres  12288 Jun 15 09:19 pg_xact ： commit log
-rw------- 1 postgres postgres     88 Jan 22 15:09 postgresql.auto.conf ： alter system
-rw------- 1 postgres postgres  26715 Apr 24 02:12 postgresql.conf : ...
-rw------- 1 postgres postgres  22761 Jan 22 15:09 postgresql.conf.bak
-rw------- 1 postgres postgres  26712 Apr 24 02:06 postgresql.confe
-rw------- 1 postgres postgres     79 Jan 30 09:57 postmaster.opts
-rw------- 1 postgres postgres    118 Jan 30 09:57 postmaster.pid : pid/datadir/start_time/port/unix_socket_dir/listen_addr/shared_mem_seg_id
```

##### PostgreSQL 表文件内部组织

一个table对应的物理文件：

```bash
[postgres@localhost 16385]$ ll 353947*
-rw------- 1 postgres postgres 11567104 Jun 15 09:51 353947
-rw------- 1 postgres postgres    24576 Jun 15 09:51 353947_fsm
-rw------- 1 postgres postgres     8192 Jun 15 09:51 353947_vm
```

###### 353947_fsm 

一个二叉树，叶子节点是某个page上的free space，一个字节代表一个page；上传节点是下层信息的汇总；用来在insert或者update的时候，找free space；

###### 353947_vm

一个bitmap，1代表page中的所有tuple对所有tranaction可见

###### 353947 : Heap Table

> vs Clustered Table

堆表，页中有新数据，直接插入进去就行，省去了调整索引的代价；但是索引上没有tmin tmax信息，tuple的事务可见性不知道，虽然可以通过vm来知道部分信息，但是还是不能避免二次读表；

索引组织表表是一个有结构的东西，有结构就会有维护结构的代价；但是良好的结构能够提高查询的速度，但是前提是查询时按照这个结构来的；换句话说，索引组织表，对于按照主键来的查询效果比较好，但是如果表上的索引多了，利用二级索引的查询效果就没那么好；

不过，内存大了，差别可能就没那么大，不同场景的权衡吧；

###### 页结构

![h](/image/heap_file_page.png)

+ PageHeader，page中各个部分的偏移
+ iterm，对应的tuple的（offset，length）
+ free space，ummmmm
+ tuple，ummmm
+ Special，如果是index，需要的的信息

##### PostgreSQL 表文件内部管理

###### 主动管理：VACUUM

+ `VACUUM [FULL]` 避免堆表的膨胀，清理dead tuple (FULL要加排他锁，重写表)；
+ `VACUUM FREEZE` 避免mvcc的xid回卷，冻结旧事物；
+ `VACUUM ANALYZE` 统计信息更新

###### 自动管理：autovacuum process

autovacuum间隔`autovacuum_naptime`执行一次`vacuum`和`analyze`命令；但是，为了避免autovacuum对系统的影响，如果autovacuum的代价超过了`autovacuum_vacuum_cost_limit`，那么就等`autovacuum_vacuum_cost_delay`这些时间；

+ **避免表膨胀**：检测到某个表中的`update，deleted`超过`autovacuum_vacuum_scale_factor*reltuples+autovacuum_vacuum_threshold`执行一次`vacuum`；
+ **更新统计信息**：`insert，update，deleted`超过`autovacuum_analyze_scale_factor*reltuples+autovacuum_analyse_threshold`执行一次`analyze`
+ **避免xid回卷**：表的`relfrozenxid`超过 `vacuum_freeze_min_age`时，vacuum就要进行freeze的操作，freeze的时候可以通过vm中的信息，来跳过一些page；而当relfrozenxid超`vacuum_freeze_table_age`时，执行vacuum的时候，就不能跳过page，必须全表freeze；而当`autovacuum_freeze_max_age`为了避免xid回卷，autovacuum freeze会强制执行；

### 存储冗余

#### 物理冗余

##### PITR

###### basebackup

###### archiver process

##### 流复制

###### wal sender process

###### wal receiver process

#### 逻辑冗余

##### Logic Replication

###### publication

###### subscription

##### pg_dump

## *高效*的计算

> 再把数据拿出来

### 单个计算——SQL解析

##### Parser

###### ParserTree

##### Analyzer

###### QueryTree

##### Rewriter

###### Rules

##### Planer

###### PlanTree

###### stats collector process

###### ScanNode (e.g.) 

###### JoinNode (e.g.) 

##### Executor

###### Pull

###### Push

### 多个计算——并发控制

#### ACID

##### 现在不要乱

###### 原子：

###### 一致：

###### 隔离：

##### 永远不要乱

###### 持久：

#### 锁

##### 锁级别

##### 如何利用锁

#### MVCC

##### 优点

##### 缺点

## *完备*的监控

> 有一个大局观

### 当前的状态——pg_catalog

##### table

##### view

### 历史的状态——日志

###### logger process

##### 监控指标