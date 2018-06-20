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

> 数据库解决两个问题：存储和计算；这两个任务能够有效的做好，完备的监控是必须的；本文从三个角度，按照自己的理解，概述一下PostgreSQL：
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

![adb](/image/arch_db.jpeg)

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

pg_basebackup命令，主要就是注意做热备的时候；wal_keep_segment可以的话，调大点

###### archiver process

注意这两个占位符：%p %f；三个配置项：mode/command/timeout；

注意开了归档，command一定是成功的，要不磁盘空间会被wal占满；

##### 流复制

PostgreSQL可以级联流复制，所以在一个基于流复制的Cluster中，有以下三个角色

###### primary master

在这个节点上，一定要设置好`wal_level`，这决定了整个集群的复制级别；10中有了logical，之前的archive和hot standby整合成replica了；如果采用同步提交打开，master上设置好`synchronous_standby_names`（有first和any两种模式）；

由于主从的查询不一样，用到的tuple也不一样；为了防止master把slave用到的tuple给清理了：

可以设置一下`vacuum_defer_cleanup_age`保留一定时间的老数据；

也可以在slave中打开`hot_standby_feedback`，来向master知会slave上的查询状态；

但是，这两种方式都可能导致bloat，所以通过`old_snapshot_threshold`强制设置一个老快照的上限；

###### cascaded slave

承接上下游的关键节点，压力还是不要太大的好

###### leaf slave

承接读流量

#### 逻辑冗余

表级复制，目前支持的是update insert delete操作的同步；

##### Logic Replication

###### publication

默认使用主键作为replication identity，没有主键必须指定一个唯一索引作为replication identity；要么只能full

```sql
REPLICA IDENTITY { DEFAULT | USING INDEX index_name | FULL | NOTHING }
```

另外，注意表结构的更改一定要同步；

###### subscription

指定publication的连接和pubname，就会在publication端创建一个logical replication slot，多个表可以共用一个slot；

##### pg_dump/pg_dumpall

常用的导出数据的命令，如果数据有坏块，得处理一下；全局的对象用dumpall，比如role等；

## *高效*的计算

> 再把数据拿出来

### 单个计算——SQL解析

##### Parser

###### ParserTree

自己拿`flex/bison`定义一个语法，就可以做语法解析了

##### Analyzer

###### QueryTree

就是把语法解析出来的tablename，columnname和数据库里的metadata对比一下

##### Rewriter

###### Rules

基于pg_rules这个用户自定义的规则，或者经验上的一些规则，比如视图展开，选择下推，重写查询；

##### Planer

###### PlanTree

基于代价估计的方式，选择node algo和path

###### stats collector process

该进程有个UDP端口，系统中的别的活动，往这里发消息来收集；

###### ScanNode (e.g.) 

各种场景下，选择哪些Scan算法：Seq 、Index、Index-Only、bitmap-index；这里注意有时候索引多了会给[PostgreSQL误导](http://yummyliu.github.io/jekyll/update/2018/06/12/%E6%B7%BB%E5%8A%A0%E7%B4%A2%E5%BC%95%E5%AF%BC%E8%87%B4PG%E7%9A%84%E6%80%A7%E8%83%BD%E6%81%B6%E5%8C%96/)

###### JoinNode (e.g.) 

nestloop、 hash、 sort-merge；PostgreSQL中的join算法还比较全，Mysql只有第一个；这也是为什么PostgreSQL在OLAP中表现比较好的原因之一；

##### Executor

###### Pull

> Copying data in memory can be a serious bottleneck. Copies contribute latency, consume CPU cycles, and can ﬂood the CPU data cache. This fact is often a surprise to people who have not operated or implemented a database system, and assume that main-memory operations are “free” compared to disk I/O. But in practice, throughput in a well-tuned transaction processing DBMS is typically not I/O-bound.
>
> ​												—— Hellerstein 
>
> ​													Architecture of a Database System

`hasNext(), next()` 

这种模型产生的时候，是IO代价比CPU代价高的时候；在这种模型中，每个tuple都需要一个next，其次，这个next的调用往往是一个虚拟函数或者函数指针的方式，此外，这种方式的代码本地性（code locality）也不好，并且会有复制的虚拟函数和函数指针记录的逻辑（book-keeping）；这样CPU消耗比较高

> code locality:
>
> ​	类似于数据缓存，利用数据访问的局部性，可以提高性能；代码的执行也是一行一行的，代码也是需要加载的，跳来跳去不太好；就比如在代码中，尽量少用goto控制语句，鼓励使用顺序，循环和分支来处理；
>
> ![nogoto](/image/nogoto.jpeg)

###### Push

在Push中，查询计划中有一些materialization points，也叫pipeline breaks；数据不是从前往后拉，是从后向前推，直到遇到某个pipeline breaks；如下图，原来的执行计划，分成了四段；

![](/image/pipeline.jpeg)

### 多个计算——并发控制

#### ACID

##### 现在不要乱

###### 原子：

事务要么成功，要么失败；关注的是事务的状态只有两种:commit和aborted

###### 一致：

整个db的数据，从外部看总是一致的；事务处理的中间状态，对外是不可见的；这是要求的强一致性，需要严格加锁；

###### 隔离：

太强的一致性，带来性能的损失；适当的降低隔离性，提高性能；

##### 永远不要乱

###### 持久：

先写日志，后写数据

#### 锁

##### 加锁的方式

###### Pre-claiming Lock

![](/image/pre_claiming.png)

在事务开始执行前，请求所需要的对象上的所有的锁；请求失败，就回滚；

###### Two-Phase Locking 2PL

![](/image/2PL.png)

事务关于锁有两个阶段，第一个阶段：只加锁，不放锁；当有一个锁被释放了，进入第二个阶段：只放锁，不加锁；

> 这种方式的2PL，由于咩有严格的隔离型，会导致cascading abort：提前释放了修改好的对象上的锁，别的事务可见了，这样如果当前事务回滚了，那么这些相关的事务，都要回滚

###### Strict Two-Phase Locking

![](/image/strict_2PL.png)

和2PL的不同就是，锁保持到事务结束，一次性释放；不会有cascading abort；

> 2PL，由于有一个持有部分锁，并等待其他锁的过程；这有可能会导致死锁；更保守的2PL协议[Conservative 2-PL](https://www.geeksforgeeks.org/dbms-concurrency-control-protocol-two-phase-locking-2-pl-iii/) 可以避免这个问题，但是实际上很少使用这个方式；了解一下。。。

###### 时间戳排序协议

每个事务有一个开始的时间戳：TS(Ti)；每个数据有一个读时间戳：R-ts(X)，和一个写时间戳：W-ts(X)；时间戳排序控制协议如下：

+ 如果事务Ti要读数据X

  ```
  If TS(Ti) < W-ts(X)
  	Operation rejected.
  If TS(Ti) >= W-ts(X)
  	Operation executed.
  All data-item timestamps updated.
  ```

  写了之后才能读；

+ 如果事务Ti要写数据X

  ```
  If TS(Ti) < R-ts(X)
  	Operation rejected.
  If TS(Ti) < W-ts(X)
  	Operation rejected and Ti rolled back.
  Otherwise, operation executed.
  ```

  数据的任何操作之后，才能写；并且老的写事务会被新的写事务忽略，这叫*[Thomas' Write Rule](https://en.wikipedia.org/wiki/Thomas_write_rule)*；

###### Graph Based Protocol

2PL只是保证了事务的*Strict Schedule*；但是没有保证*deadlock free*；Graph Based Protocol是一个可选方案，tree based Protocol是一个简单的实现；比较复杂了，没细看；

##### 锁类型

###### 表

###### 行

###### 页

###### 咨询

###### 死锁

#### MVCC

[mvcc in pg](http://yummyliu.github.io/jekyll/update/2018/05/30/Mvcc_And_Vacuum/)

## *完备*的监控

> 有一个大局观

### 当前的状态——pg_catalog

##### table

基本就是一些PostgreSQL中的一些概念的原信息，pg_trigger /pg_type/ pg_class/ pg_index/ pg_sequence 。。。

##### view

pg_stat_*

### 历史的状态——日志

##### logger process

+ where

  + log_destination：stderr, csvlog , syslog；csvlog需要`logging_collector = on`
  + log_directory：
  + log_filename

+ when

  + log_min_duration_statement

+ what

  + log_connections
  + log_checkpoints
  + log_duration
  + log_lock_waits
  + log_statement
  + log_temp_files

+ csv to table

  ```sql
  CREATE TABLE postgres_log
  (
    log_time timestamp(3) with time zone,
    user_name text,
    database_name text,
    process_id integer,
    connection_from text,
    session_id text,
    session_line_num bigint,
    command_tag text,
    session_start_time timestamp with time zone,
    virtual_transaction_id text,
    transaction_id bigint,
    error_severity text,
    sql_state_code text,
    message text,
    detail text,
    hint text,
    internal_query text,
    internal_query_pos integer,
    context text,
    query text,
    query_pos integer,
    location text,
    application_name text,
    PRIMARY KEY (session_id, session_line_num)
  );
  COPY postgres_log FROM '/full/path/to/logfile.csv' WITH csv;
  ```

+ process title set cluster_name；一个机器上多个实例，可以区分一下；

##### 监控指标

[PostgreSQL监控指标整理](http://yummyliu.github.io/jekyll/update/2018/06/01/pgwatch2%E8%A7%A3%E6%9E%90/)

[ref](https://www.cl.cam.ac.uk/teaching/2000/ConcSys/csig2/57.html)

