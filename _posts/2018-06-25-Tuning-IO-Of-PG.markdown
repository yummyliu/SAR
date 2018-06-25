---
layout: post
title: 
date: 2018-06-25 20:46
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - PostgreSQL
typora-root-url: ../../yummyliu.github.io
---

为了确保数据库在指定规模下，有期望的响应时间，需要认证调整一些参数；然而，数据库的性能会因为许多原因变坏，比如基础设施的规模限制，低效地数据库维护策略，不好的SQL代码或者数据库进程配置不当导致不能完全利用硬件（CPU 内存，网络带宽，磁盘IO）性能；

#### 导致数据库性能下降的原因有哪些？

+ 不好的SQL查询使用了差的join，和 其他逻辑等等，导致使用了很多CPU和内存
+ 由于不当的索引，导致查询使用了全表扫描
+ 数据库维护不当，导致统计信息更新不及时
+ 错误的容量规划，导致基础设施不足
+ 不当的逻辑和物理设计
+ 没有使用链接池，应用发起了太多的链接；

因此，这有很多可能导致数据库性能下降；本文中，主要讲其中最重要的——调整PostgreSQL的IO性能，这是很重要的，特别是在高事务量的OLTP应用中，或者多去大量数据的数据仓库环境中；

​	很多时候，数据库的性能瓶颈就是高IO，确保数据库是针对IO优化的很重要。      

#### 调整PostgreSQL的IO

##### 索引

表上有索引，插入数据时，会引起写放大，如果上面有函数索引，会更加糟糕；避免这一问题，我们可以给数据库服务配置多个的磁盘文件系统，将索引和磁盘的放在不同的表空间中。作为dba管理index的建议：

+ 理解索引的需求，建一个明智的索引
+ 避免创建多个索引，并且创建一些不需要的索引，这会拖累系统
+ 监控索引的使用情况，删除不需要的索引
+ 当索引列的数据更改了，索引会膨胀。因此定期的重建索引的必要的；

##### 分区

一个高效的分区策略能够改善IO性能问题。大表可以基于业务逻辑拆分。PostgreSQL支持表分区。尽管他不能支持分区表的全部特性，只能在实时的用户场景中使用。在PostgreSQL中，分区的子表是和主表完全独立的，这也会是一个瓶颈，比如，在master上的约束条件不能自动继承到子表中。

不管怎样，对应平衡IO来说，分区是很有帮助的。所有的子分区能够放在不同的表空间和磁盘文件系统中。基于日期列的查询，根据where条件定位到相应的时间分区中，比起全表扫描，这有很大的性能提升。

##### 检查点

检查点定义了数据库的一致性状态。他们十分关键，并且检查点的定期执行时很重要的，确保数据变化永久保存到磁盘中并且数据库的状态是一致的。这就说明，不当的检查点配置会导致IO性能问题。DBA需要关注检查点的配置，确保没有任何IO的尖刺，这取决于磁盘性能的好坏，以及数据文件的组织。

###### 检查点干了什么事？

简单来说，检查点确保：

+ 所有提交的数据，被写入到磁盘中
+ clog文件更新了提交状态
+ 循环利用pg_xlog(pg_wal)中的事务日志文件

这解释了IO敏感的检查点是什么。配置文件中有一些参数可以调整检查点的行为：

+ max_wal_size
+ min_wal_size
+ checkpoint_timeout
+ checkpoint_completion_target

这些配置决定了检查点执行的频率，以及检查点多长时间结束，关于配置检查点的一些建议：

+ 评估数据库的TPS。评估全天事务数，什么时间达到峰值

+ 和应用开发者以及其他了解数据库事务率和未来事务增长的技术团队讨论

+ 单从数据库端，我们可以做：

  + 监控数据库，评估全天的事务数。这可以查询pg_catalog.pg_stat_user_tables得到

  + 评估每天产生的归档日志数

  + 打开`log_checkpoints`参数，监控检查点的进行，去理解检查点的行为。

    ```
    2018-06-26 04:50:02.644 CST,,,57574,,5978e06e.e0e6,19349,,2017-07-27 02:33:18 CST,,0,LOG,00000,"checkpoint starting: time",,,,,,,,,""
    2018-06-26 05:35:03.272 CST,,,57574,,5978e06e.e0e6,19350,,2017-07-27 02:33:18 CST,,0,LOG,00000,"checkpoint complete: wrote 595866 buffers (12.6%); 0 transaction log file(s) added, 0 removed, 1498 recycled; write=2610.601 s, sync=0.263 s, total=2700.628 s; sync files=520, longest=0.126 s, average=0.000 s",,,,,,,,,""
    ```

  + 打开`checkpoint_warning`参数，默认是30s；在一个`checkpoint_timeout`时间内，如果wal_size超过了`max_wal_size`，那么会触发一次checkpoint；当在`checkpoint_timeout`期间，checkpoint被频繁的触发（即，间隔时间小于checkpoint_warning），那么就需要提高`max_wal_size`；日志中会提示：

    ```
    2018-06-06 15:02:42.295 IST [2111] LOG:  checkpoints are occurring too frequently (11 seconds apart)
    2018-06-06 15:02:42.295 IST [2111] HINT:  Consider increasing the configuration parameter "max_wal_size".
    ```

##### 带有FILLFACTOR的VACUUM和ANALYZE

VACUUM和ANALYZE需要全表扫描来对表做维护操作，详细情况可以看[另一个博客](http://yummyliu.github.io/jekyll/update/2018/03/30/autovacuum/)；这里讲一下FILLFACTOR；

这个参数表示在表的数据块中给insert使用的空间，默认是100%；这意味着insert可以使用全部的空间，同样意味着，update没有空间使用；

可以在create table和create index的时候指定，当FILLFACTOR参数设置合理时，表空间不会增长太快，VACUUM的性能和查询的性能就会不一样；

> NOTE：当在一个已经存在的表上，更改FILLFACTOR；需要VACUUM FULL或者重建这个表，确保生效；

###### 对于VACUUM的一些建议：

+ 在高负载的用户表上每晚手动执行VACUUM ANALYZE
+ 在批量insert之后，执行VACUUM ANALYZE。这很重要但是很多人认为在INSERT之后不需要执行VACUUM
+ 监控重要的表上的vacuum状态（pg_stat_tables），确保被定期vacuum了
+ 使用pg_stattuple，确认表空间膨胀大小
+ 不能在生产环境执行VACUUM FULL，使用pg_reorg或者pg_repack重建表和索引；
+ 确保高负载系统中，AUTOVACUUM是在执行的
+ 打开log_autovacuum_min_duration
+ 在高负载的表上，打开FILLFACTOR

##### 磁盘上外排序

执行GROUP BY, ORDER BY, DISTINCT, CREATE INDEX, VACUUM FULL等操作，会执行排序操作，并且可能在磁盘中进行；如果有索引，可以在内存中进行，这时候复合索引发挥了他的作用；否则，如果sort降级为使用磁盘，性能大幅下跌；

使用work_mem，来确保排序在内存中发生。可以在配置文件中配，也可以在会话层，表层，用户层，数据库层配置。通过打开`log_temp_files`配置（以bytes为单位），我们可以判断需要多少空间；打开之后，我们可以在日志中看到：

```bash
2018-06-07 22:48:02.358 IST [4219] LOG:  temporary file: path "base/pgsql_tmp/pgsql_tmp4219.0", size 200425472
2018-06-07 22:48:02.358 IST [4219] STATEMENT:  create index bid_idx on pgbench_accounts(bid);
2018-06-07 22:48:02.366 IST [4219] LOG:  duration: 6421.705 ms  statement: create index bid_idx on pgbench_accounts(bid);
```

上述信息，表示CREATE INDEX这个查询，需要200425472 bytes；因此work_mem设置为大于等于200425472 bytes，可以在内存中进行排序；

对于应用的查询，只能在用户级别配置work_mem；做这个之前了解一下该用户的了解连接数，防止oom；

##### 关于数据库文件系统结构的建议

确保数据库使用多个表空间

+ 将表和索引放在不同的表空间中
+ 表空间放在不同的磁盘中
+ pg_wal放在单独的磁盘中
+ 确保*_cost的配置和底层是相同的
+ 使用iostat或者mpstat等监控工具，时常关注读写状态；

##### 批量数据加载的建议

基于数据加载的操作一般是在还未上线的时候进行的的前提，确保下面配置好，能够加速数据加载

+ 调大checkpoint的相关配置
+ 关闭`full_page_write`
+ 关闭wal归档
+ 关闭`synchronous_commit`
+ 去掉索引和约束，后面基于很大的work_mem重新创建
+ 如果从csv中加载，调大`maintenance_work_mem`
+ **不要关闭**fsync，尽管这有很大的性能提升，但是会导致数据损坏。













 

