---
layout: post
title: 
date: 2018-03-30 15:13
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
---



### dead tuples

在PG里，delete设置tuple的xmax，tuple还在表中；update就是delete+insert；如果一些元祖对所有的事务都不可见了，那么就是dead tuple，占用磁盘空间，并且index还引用他们，会导致表和索引的膨胀；

### Vacuum、autovacuum

VACUUM 操作会重新回收dead tuple的磁盘空间，但是不会交给OS，而是为新tuple留着；

> VACUUM FULL 会把空间返回给OS，但是会有一些弊端；
>
> 1. 排他的锁表，block all ops
> 2. 会创建一个表的副本，所以会将使用的磁盘空间加倍，如果磁盘空间不足，不要执行

VACUUM是手动执行的，虽然可以利用定时任务周期执行，但是周期的大小不确定，而且有可能这个周期内并没有dead tuple产生，这样就徒增CPU和IO的负载；

`autovacuum`是按需执行的；DB在事务执行的时候统计 delete和update的数量；当dead tuple积攒到一定数量时候，触发autovacuum（默认是20%）；

autoanalyze：autovacuum 除了干回收的事，还同时统计信息；

### 监控

+ 表中dead tuple数量：`pg_stat_all_tables.n_dead_tup`
+ dead/live比例：`(n_dead_tup / n_live_tup)`
+ 每行的空间：`select relname,pg_class.relpages / pg_class.reltuples from pg_class where reltuples!=0;`
+ 插件：pgstattuple

### 调优的目标

+ 清理dead tuple；不浪费磁盘空间、预防索引膨胀、保证查询响应速度
+ 最小化清理的影响；不要清理的太频繁，这样浪费CPU IO资源，影响性能

#### 参数：阈值和膨胀系数

控制autovacuum触发的时间；

+ `autovacuum_vacuum_threshold=50`
+ `autovacuum_vacuum_scale_factor=0.2`

当`pg_stat_all_tables.n_dead_tup` 超过 `threshold + pg_class.reltuples * scale_factor`就会触发autovacuum；表膨胀超过20%就触发，threshold=50是防止一些小表被频繁的触发；

```sql
ALTER TABLE t SET (autovacuum_vacuum_scale_factor = 0.1);
ALTER TABLE t SET (autovacuum_vacuum_threshold = 10000);
```

#### 节流阀

为了不影响用户使用，不过多的占用cpu io；

清理的进程从disk中逐个的读取page（8k），判断其中有没有dead tuple，如果没有那么就不管了；如果有，就将其中的dead tuple清理掉，然后标记为dirty，最后写出去；这一过程的cost基于一下三个参数来确定，这样我们可以评估`autovacuum`的代价;

```
vacuum_cost_page_hit = 1
vacuum_cost_page_miss = 10
vacuum_cost_page_dirty = 20
```

如果page是从share buffer中读取的，代价为1；如果share buffer中没有，代价为10；如果被清理进程标记为dirty，代价为20；

基于以上的代价，每次autovacuum执行有一个cost的限制；如下

```
autovacuum_vacuum_cost_delay = 20ms
autovacuum_vacuum_cost_limit = 200
```

默认是200，执行代价总数为200的工作；每次工作完，间歇20ms；

那么实际上执行的多少工作？基于20ms的间隔，cleanup能够每秒做50轮；每轮能够做200cost；那么：

+ 从shared_buffer读page，80MB/s 
+ 从OS（可能是磁盘）读page，8MB/s
+ 4Mb/s的速度写出autovacuum标记的脏页

基于当前的硬件，这些参数都太小了，cost_limit可以设置成1000+

#### 工作进程数

db可以启动`autovacuum_max_workers`的进程来清理不同的数据库/表；大表小表的代价不同，这样不会因为大表的工作，阻塞了小表；但是上面的cost_limit是被所有的worker共享的，所以开多个worker也不一定快。

所以，当清理进程跟不上用户活动时，提高worker数是不行的，要提高改变的cost参数；

#### 每个表的节流阀

上面说到costlimit是全局的，被所有worker共享的；这里其实可以在单独的表上设置这两个值：

```sql
ALTER TABLE t SET (autovacuum_vacuum_cost_limit = 1000);
ALTER TABLE t SET (autovacuum_vacuum_cost_delay = 10);
```

这些表的清理工作的cost，不包含在global中；单独计算；提高了一定的灵活性；实际生产中，我们几乎不会用这个特性：

1. 一般需要一个唯一的全局后台清理cost限制
2. 使用多个worker，有时被一起限制，有时单独来，很难监控和分析；

### 总结

1. 不要禁用autovacuum
2. 在update delete频繁的业务中，减低scale factor，这样清理进程可以及时的进行
3. 在好的硬件下，提高限流阀，这样清理进程不会被中断
4. 单独提高autovacuum_max_worker不行，需要和参数一起调整
5. 使用alter table设置参数要慎重，这会让系统变得复杂；

[ref](https://blog.2ndquadrant.com/autovacuum-tuning-basics/#PostgreSQL Performance Tuning)
