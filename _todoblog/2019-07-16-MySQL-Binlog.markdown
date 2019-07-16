---
layout: post
title: MySQL BinLog 详解
date: 2019-07-16 18:08
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---
> * TOC
{:toc}

# MySQL的BinLog

在MySQL中，除了存储引擎InnoDB的日志外，自身还有一个BinLog，其中记录了对数据进行了修改（以及可能对数据有修改）的事件，主要用在主从复制和PITR场景中。

> BinLog除了记录修改数据的操作，还会记录元信息：
>
> - 确保操作重放的正确的额外信息
> - 错误码
> - binlog自身的维护信息，比如rotate事件。
>
> 除了Redo、undo、binlog这些用于恢复的日志，MySQL中还有一些用于记录的日志：Errorlog、General Query Log、Slow Query Log、DDL Log。

每个binlog的大小由`max_binlog_size`决定，当超过该参数大小，会切换到一个新的文件；注意，同一个事务的binlog应该放在一个binlog文件中，当存在一个很大的事务，可能会超过`max_binlog_size`。

在启动的时候，由binlog-format指定三种类型：statement、row、mixed。

- statement：记录主上执行的语句

- row：以主键为标识，记录相应行的变更。

- mixed：默认使用statement，当MySQL认为[某个语句不能完全复制](https://dev.mysql.com/doc/refman/5.7/en/binary-log-mixed.html)，升级为记录row。

  

# Group Commit

在PostgreSQL中有一个参数[commit_delay](https://www.postgresql.org/docs/current/runtime-config-wal.html#GUC-COMMIT-DELAY)代表PostgreSQL中的组提交，即，相比于每个事务都刷盘，打开这个参数，PostgreSQL会等待一组事务然后一起提交；但并发高的时候能提高整体的吞吐量，但是在并发较低的时候。

当然，前提是PostgreSQL打开了`fsync`参数；另外考虑到并发低的时候，没有必要等待；这里当db的活动事务大于`commit_siblings`时，才会delay commit(group commit)。

同样地，在MySQL中为了解决fsync带来的tps吞吐瓶颈问题，也有Group Commit特性，但是MySQL中由于存储的redolog和上层的binlog需要保证XA一致性，因此实现起来相对复杂。

## binlog与redolog的一致性

InnoDB是MySQL默认支持的事务型存储引擎，当上层同时启用binlog时，为了保证上下日志的一致，需要采用XA 2pc进行两阶段提交。

1. Prepare InnoDB

   1. 在InnoDB的logbuffer中，写入prepare记录；
   2. **fsync**：日志文件刷盘；
   3. 获取`prepare_commit_mutex`。

2. Prepare binlog

   1. 将事务写入binlog；

   2. **fsync**：基于`sync_binlog`配置的行为，进行binlog刷盘。

      > **XA recovery**
      >
      > 如果这里binlog刷盘了，那么恢复的时候，该事务认为是提交的；如果在这之前crash了，该事务会被回滚。

3. Commit InnoDB

   1. 在logbuffer中，写入commit记录；
   2. 释放`prepare_commit_mutex`;
   3. **fsync**：日志文件刷盘
   4. 释放InnoDB的锁

4. Commit binlog

   1. 没什么特别需要做的

在多个事务并发的进行2PC提交的时候，redolog的写入顺序和binlog的写入顺序可能不一致；基于`prepare_commit_mutex`，确保三次刷盘操作的顺序，从而保证binlog与InnoDB的redolog的顺序是一致的。

> MySQL的主从复制一般基于binlog实现；

然而，由于存在这样一个互斥同步，导致第2步的binlog**不能进行组提交**；并且理想情况下，一个事务，只有一个fsync操作即可，然而这里进行三次，**性能上也不太乐观**；没有对此进行优化的之前，binlog除了作为存储模块还是事务调度模块，**模块不清晰**，不易于维护。

## binlog的group commit

上述基于`prepare_commit_mutex`的commit机制的临界区过大，使得整个并发度降低，从而整体的吞吐降低。

因此，将binlog的提交划分为三个阶段，每个阶段都有一个执行队列：

- flush：write
- sync：刷盘
- commit：成功

进入某阶段的第一个thread作为leader，后续进来的都是follower；leader将该阶段的threads注册到下一阶段中，然后统一负责处理该阶段的任务（如果下一阶段不为空，那么该leader成为下一阶段的follower，因此最慢的sync阶段会累积很多任务），此时follower就是等待处理完成的通知；

```bash
8.0

cmake . -DFORCE_INSOURCE_BUILD=1

make

/Users/liuyangming/src/mysql-8.0.16/runtime_output_directory
```

