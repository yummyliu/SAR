---
layout: post
title: 深入认识PostgreSQL的Checkpoint
subtitle: checkpoint是保证某一时间点的数据已经同步到磁盘中了，这样恢复的时候可以快一些
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
---

## what

checkpoint会将某个时间点前的全部脏数据刷新到磁盘，以实现数据的一致性与完整性。

+ 持久性，redo日志，先写日志；
+ 用户只需要等待wal flush；日志是顺序写，数据是随机的；
+ 数据是GB的，wal是TB的；保存所有的wal是不现实的；
+ 保证在wal的某个位置之前的所有change，都已经写到磁盘中了；
+ 减少recovery时间，之前的wal理论上就可以删了；

> NOTE: 目标是，checkpoint以不影响用户使用的频率执行；

![checkpoint](/image/fig-9-13.png)

1. 在checkpoint进程启动的时候，在内存中记录一个REDO point‘；REDO point就是db开始恢复XLOG的位置
2. 关于这个checkpoint的xlog record写入到wal buffer中
3. 将所有的shard Memory刷新到存储中（包括clog）；
4. shard buffer 中的dirty page**逐渐地**刷新到磁盘中
5. 更新pg_control文件，其中有checkpoint的lsn信息（后期恢复可以从这个文件读取checkpoint的lsn）

## 触发checkpoint

1. 超级用户（其他用户不可）执行CHECKPOINT命令
2. 间接执行：pg_start_backup/create database/pg_ctl stop/restart...
3. 到达配置中配置的执行checkpoint的间隔时间
4. 到达配置中配置的wal数量限制（running out of wal，filling wal）
5. 数据库recovery完成
6. 需要刷新所有脏页

> 以上触发刷脏数据，PG还有个时间点，如果设置了归档，就是何时触发归档；其实就是只要每次产生了一个新的wal，就执行archive_commond:
>
> 1. 主动执行 `pg_switch_xlog()`
> 2. wal写满
> 3. archive_timeout到达

### 周期checkpoint配置

time和size的限制

+ checkpoint_timeout = 5min
+ max_wal_size = 1GB (9.5之前叫：checkpoint_segments)

> NOTE: `max_wal_size`不是一个强制的限制

设置参数的原则

1. 选择一个合理的`checkpoint_timeout`值，这个取决于需要的恢复时间

   一般来说，默认的5分钟有点低，取30min到1h比较常见。9.6之后最大值可以设置长1day；由于full-page write，太小的值可能导致写放大。

2. 把`max_wal_size`设置几乎不可能达到的足够大

   + 使用`pg_current_xlog_insert_location()`

     ```sql
     postgres=# SELECT pg_current_xlog_insert_location();
      pg_current_xlog_insert_location 
     ---------------------------------
      3D/B4020A58
     (1 row)

     ... after 5 minutes ...

     postgres=# SELECT pg_current_xlog_insert_location();
      pg_current_xlog_insert_location 
     ---------------------------------
      3E/2203E0F8
     (1 row)

     postgres=# SELECT pg_xlog_location_diff('3E/2203E0F8', '3D/B4020A58');
      pg_xlog_location_diff 
     -----------------------
                 1845614240
     (1 row)
     ```

     5分钟产生了1.8g的wal日志

   + log_checkpoints=on

     从日志里看checkpoint的统计信息

   + select * from pg_stat_bgwriter

   ### Spread checkpoint

   ##### pg_stat_bgwriter

   ```sql
                           View "pg_catalog.pg_stat_bgwriter"
           Column         |           Type           | Collation | Nullable | Default
   -----------------------+--------------------------+-----------+----------+---------
    checkpoints_timed     | bigint                   |           |          |
    checkpoints_req       | bigint                   |           |          |
    checkpoint_write_time | double precision         |           |          |
    checkpoint_sync_time  | double precision         |           |          |
    buffers_checkpoint    | bigint                   |           |          |
    buffers_clean         | bigint                   |           |          |
    maxwritten_clean      | bigint                   |           |          |
    buffers_backend       | bigint                   |           |          |
    buffers_backend_fsync | bigint                   |           |          |
    buffers_alloc         | bigint                   |           |          |
    stats_reset           | timestamp with time zone |           |          |
   ```

   pg中的background writer是用来执行checkpoint的，其中`checkpoints_timed`记录了周期性执行的checkpoint的次数；`checkpoint_req`记录的是主动请求的checkpoint的次数；

   在PG中，记录的更改是先记录到wal日志中的；除非shared_buffer满了，才会将脏页写出去；这意味着当系统crash了，启动的时候需要重做wal日志；

   在checkpoint期间，db需要执行以下三个步骤；

   1. 确认shared buffer中，所有的脏页
   2. 将所有的脏页写盘（或者在文件系统缓存中）
   3. fsync() to disk

   本来PG是一下做完的，但是这样会使IO夯住，影响用户使用。在8.3之后，引入了spread checkpoint，这给了OS时间将脏页写出去，最后的fsync就没那么耗时了；

   > NOTE: 这里有一些OS参数控制着文件系统缓存；比如
   >
   > 1. vm.dirty_expire_centisecs
   > 2. vm.dirty_background_bytes
   > 3. dirty_background_ratio
   >
   > 在一些有超大内存的机器上，这些值的默认值很高；这会导致系统倾向于一次将所有的脏页写出，这就使得spread checkpoint没有作用了

数据库参数`checkpoint_completion_target = 0.5`，意味着当checkpoint_timeout等于30min的时候，在前15分钟，数据库将所有的写操作完成，之后的15分钟留给文件系统去fsync；一般不会设置成0.5,  毕竟文件系统刷新缓存也是有上面的参数限制的。通常的公式如下

```
(checkpoint_timeout - 2min) / checkpoint_timeout
```



### 总结

1. 大部分的checkpoint是基于时间的 checkpoint_timeout；
2. 选定好时间间隔后，选择max_wal_size来估计wal大小
3. 设置checkpoint_completion_target，这样内核有足够的时间（但是不需要太多）来刷盘
4. 修改系统参数vm.dirty_background_bytes，这样OS不会建大部分的脏页，一次写盘

[ref](https://blog.2ndquadrant.com/basics-of-tuning-checkpoints/)

[dd](http://www.interdb.jp/pg/pgsql09.html)
