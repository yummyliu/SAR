---
layout: post
title: 认识PostgreSQL的Lock
subtitle: PostgreSQL的DML操作使用锁，DDL操作的并发使用2PL协议
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
---

{:toc}

PostgreSQL提供了**各种级别的锁**来控制对**各种数据对象**的并发访问。大多数PostgreSQL命令会自动获取适当模式的锁，以确保引用的对象不会在执行命令时，被冲突的事务删除或修改。本文简单介绍下PostgreSQL中的各种锁，以及可能的应用。如果读了这个文章对PostgreSQL中的锁感兴趣，可以参考官方文档或者PostgreSQL源码。

PostgreSQL中有多种类型的锁，本文中按照用户是否可见将其分为两种。

### 用户可见的（pg_locks）

用户可见的锁，是用户自己能够主动调用，并且在pg_locks中看到是否grant的锁，有regular lock和咨询锁。

#### regular Lock

regular lock分为表级别和行级别两种。

##### 表级别

一些操作可以自动的获得一些锁，也可以用`LOCK`语句显示的获得某些锁。在一个事务中可以定义 很多savepoint，当rollback to 某个sp时，会释放该sp之后获得的锁。

##### 行级别

通过一些数据库操作自动获得一些行锁，行锁并不阻塞数据查询，只阻塞writes和locker，比如如下操作。

- FOR UPDATE
- FOR NO KEY UPFATE
- FOR SHARE
- FOR KEY SHARE

PostgreSQL同一时间修改的行数没有限制，这里如果想避免获得不了锁的等待，可以采用`FOR UPDATE NOWAIT` ，或 `FOR UPDATE SKIP LOCKED`的方式。

##### 死锁

​	显式加锁可能会导致死锁，PostgreSQL检测到死锁会cancel其中的一个事务，日志中会有相应的记录；为了避免死锁，一定要注意长事务的监控。

#### 咨询锁

​	当MVCC模型和锁策略不符合应用时，采用咨询锁。咨询锁是提供给应用层显示调用的锁方法，在表中存储一个标记位能够实现同样的功能，但是咨询锁更快；其能避免表膨胀，且会话（或事务）结束后能够被Pg自动清理。

获得咨询锁：

1. 会话级别：该级别获得的咨询锁，没有事务特征，事务回滚或者取消，之前获得的咨询锁不会被unlock，一个咨询锁可以多次获得，相应的要多次取消。
2. 事务级别：事务级别获得的锁，事务结束后会自动unlock。

两种方式获得的咨询锁，如果锁在了一个识别符上，那么他们也是互相block的；咨询锁可以用在业务需要强制串行化等场景中，比如秒杀。

#### 注意点

##### 内存耗尽

锁是存储在内存中的，上限由参数max_locks_per_transaction 和 max_connections控制，要避免空间耗尽，导致无法获取新的Lock。

##### 咨询锁与limit

使用咨询锁时，如果有limit操作，要注意可能pg_advisory_lock在limit操作之前调用，那么如下的情况可能并不是锁了100个对象。

```sql
SELECT pg_advisory_lock(id) FROM foo WHERE id = 12345; -- ok
SELECT pg_advisory_lock(id) FROM foo WHERE id > 12345 LIMIT 100; -- danger!
SELECT pg_advisory_lock(q.id) FROM
(
  SELECT id FROM foo WHERE id > 12345 LIMIT 100
) q; -- ok
```

##### 禁止加带默认值的列

>（PostgreSQL10之前）

PostgreSQL10之前，果你添加的列带有默认值，PostgreSQL会重写整张表，来对每一行设置默认值，在大表上可能会是几个小时的工作，这样所有查询就会被阻塞，数据库不可用；

DO NOT

```SQL
-- 读写都会被阻塞
ALTER TABLE items ADD COLUMN last_update timestamptz DEFAULT now();
```

INSTEAD

```SQL
-- select, update, insert, 和 delete 都会阻塞
ALTER TABLE items ADD COLUMN last_update timestamptz;
-- select 和 insert 可行, 当表重写的时候，部分update·和delete会被阻塞
UPDATE items SET last_update = now();
```

BETTER

```c
do {
  numRowsUpdated = executeUpdate(
    "UPDATE items SET last_update = ? " +
    "WHERE ctid IN (SELECT ctid FROM items WHERE last_update IS NULL LIMIT 5000)",
    now);
} while (numRowsUpdate > 0);
```

##### 理解锁队列，使用lock_timeout

每个PostgreSQL中的锁都有一个锁队列。如果一个锁是排他的，事务A占有，事务B获取的时候，就会在锁队列中等待。有趣的是，如果这时候事务C同样要获取该锁，那么它不仅要和A检查冲突性，也要和B检查冲突性，以及队列中其他的事务；这就意味着，即使你的DDL语句可以很快的执行，但是它可能会在队列中等待很久，直到前面的查询结束。并且该DDL操作会将后续的查询阻塞；

DO NOT

```sql
ALTER TABLE items ADD COLUMN last_update timestamptz;
```

INSTEAD

```sql
SET lock_timeout TO '2s'
ALTER TABLE items ADD COLUMN last_update timestamptz;
```

##### CREATE INDEX CONCURRENTLY

在一个大数据集上建索引，有可能会花费数小时甚至数天的时间；常规的create index会阻塞所有的写操作；尽管不阻塞select，但是这还是不好的；

并行的创建索引确实有缺点。如果出了问题，它不会回滚，这会留下一个未完成的index；但是不用担心，`DROP INDEX CONCURRENTLY items_value_idx`，重新创建即可。

##### 晚点使用激进的锁

当在一个表上执行需要获得激进策略锁的时候，越晚越好，影响越小；比如如果你想替换一个表的内容；

DO NOT

```sql
BEGIN;
-- 读写都被阻塞
TRUNCATE items;
-- long-running operation:
\COPY items FROM 'newdata.csv' WITH CSV 
COMMIT; 
```

Instead

```sql
BEGIN;
CREATE TABLE items_new (LIKE items INCLUDING ALL);
-- long-running operation:
\COPY items_new FROM 'newdata.csv' WITH CSV
-- 读写从这开始阻塞
DROP TABLE items;
ALTER TABLE items_new RENAME TO items;
COMMIT; 
```

这里有个问题，我们不从一开始阻塞写。这样老的items表，在我们drop它之前，会发生改变；为了避免这一个情况，可以在一开始将表锁住，阻塞写，但是不阻塞读；

```SQL
BEGIN;
LOCK items IN EXCLUSIVE MODE;
...
```

##### 添加主键的时候最小化锁阻塞

在表上添加一个主键是有意义的，PostgreSQL中，可以通过alter table很方便的添加一个主键，但是当主键索引创建的时候，会花费很长时间，这样会阻塞查询；

DO NOT

```sql
ALTER TABLE items ADD PRIMARY KEY (id); 
```

INSTEAD

```sql
CREATE UNIQUE INDEX CONCURRENTLY items_pk ON items (id); -- 花很长时间，但是不会阻塞读写
ALTER TABLE items ADD CONSTRAINT items_pk PRIMARY KEY USING INDEX items_pk;  -- 阻塞读写，但是很短
```

通过将主键索引的创建，分成两步；这样繁重的创建索引的工作不会影响业务查询；

##### 调整命令顺序，避免死锁

如下两个事务,会导致死锁

```sql
BEGIN;
UPDATE items SET counter = counter + 1 WHERE key = 'hello'; -- grabs lock on hello
UPDATE items SET counter = counter + 1 WHERE key = 'world'; -- blocks waiting for world
END;
```

```SQL
BEGIN
UPDATE items SET counter = counter + 1 WHERE key = 'world'; -- grabs lock on world
UPDATE items SET counter = counter + 1 WHERE key = 'hello';  -- blocks waiting for hello
END; 
```

在一应用中，调整调用顺序，避免互相锁住对方

### 用户不可见

#### 自旋锁

Vs 原子操作

#### 页锁（lightweight lock）

​	PG中，有对于表的数据页的共享/互斥锁，一旦这些行读取或者更改完成后，相应锁就被释放。应用开发者一般不用考虑这个锁。

https://www.percona.com/blog/2018/10/30/postgresql-locking-part-3-lightweight-locks/

### 参考

[tips_for_deal_locks](https://www.citusdata.com/blog/2018/02/22/seven-tips-for-dealing-with-postgres-locks/)