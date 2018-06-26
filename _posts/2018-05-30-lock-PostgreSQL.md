---
layout: post
title: PostgreSQL的并发控制——锁
subtitle: PostgreSQL的DML操作使用锁，DDL操作的并发使用2PL协议
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
---

PostgreSQL提供了各种锁定模式来控制对表中数据的并发访问。 锁可用于当MVCC不满足MVCC不满足需求时，由应用程序控制锁定。 此外，大多数PostgreSQL命令会自动获取适当模式的锁定，以确保引用的表不会在执行命令时，被冲突的事务删除或修改。 （例如，当同一个表上有其他操作在执行时，TRUNCATE不能同时执行，所以它会在表上获得排它锁后，执行该操作。）

​	使用pg_locks系统视图，可以看到现在系统中的锁。

#### 表锁

一些操作可以自动的获得一些锁，也可以用`LOCK`语句显示的获得某些锁。

- ACCESS SHARE
  - 只读的查询在相应的表上，获得这个锁
  - ！EXCLUSIVE
- ROW SHARE
  - SELECT FOR UPDATE/ FOR SHARE
  - ! EXCLUSIVE
- ROW EXCLUSIVE
  - 修改表的操作
  - ！ SHARE, SHARE ROW EXCLUSIVE, EXCLUSIVE, ACCESS EXCLUSIVE
- SHARE UPDATE EXCLUSIVE
  - VACUUM (without FULL), ANALYZE, CREATE INDEX CONCURRENTLY, ALTER TABLE VALIDATE and other ALTER TABLE variants
  - ! SHARE UPDATE EXCLUSIVE, SHARE, SHARE ROW EXCLUSIVE, EXCLUSIVE, and ACCESS EXCLUSIVE
- SHARE
  - Create index； 阻止当前表数据的更改
  - ！ ROW EXCLUSIVE, SHARE UPDATE EXCLUSIVE, SHARE ROW EXCLUSIVE, EXCLUSIVE, and ACCESS EXCLUSIVE
- SHARE ROW EXCLUSIVE
  - CREATE TRIGGER / ALTER TABLE
  - ! ROW EXCLUSIVE, SHARE UPDATE EXCLUSIVE, SHARE, SHARE ROW EXCLUSIVE, EXCLUSIVE, and ACCESS EXCLUSIVE lock
- EXCLUSIVE
  - REFRESH MATERIALIZED VIEW CONCURRENTLY（锁是在视图上，还是在底层的表上？）
  - ！ ROW SHARE, ROW EXCLUSIVE, SHARE UPDATE EXCLUSIVE, SHARE, SHARE ROW EXCLUSIVE, EXCLUSIVE, and ACCESS EXCLUSIVE
- ACCESS EXCLUSIVE
  - DROP TABLE, TRUNCATE, REINDEX, CLUSTER, VACUUM FULL, and REFRESH MATERIALIZED VIEW (without CONCURRENTLY)
  - 和所有的模式冲突，只能有一个事务访问当前这个表
  - tip：只有这个锁才能阻塞 select（without for update、share）查询

在一个事务中可以定义 很多savepoint，当rollback to 某个sp时，会释放该sp之后获得的锁。

#### 行锁

通过一些数据库操作自动获得一些行锁，行锁并不阻塞数据查询，只阻塞writes和locker。

- FOR UPDATE
  - 会阻塞这些操作`UPDATE`, `DELETE`, `SELECT FOR UPDATE`, `SELECT FOR NO KEY UPDATE`, `SELECT FOR SHARE`or `SELECT FOR KEY SHARE`，防止这些操作 修改、删除、锁定这些行。
  - Delete某一行； Update某一行的某一列（该列上有唯一索引）；select  for update；这三中情况都会锁住相应的行
- FOR NO KEY UPFATE
  - 比FOR UPDATE，级别低，不会阻塞其他 select for key share。
  - update的时候没有获得FOR update 锁的，会会获得这个锁
- FOR SHARE
  - 和FOR UPDATE相似，但是获得的是SHARE锁，而不会EXCLUSIVE锁
  - 阻塞其他的`UPDATE`, `DELETE`, `SELECT FOR UPDATE` or `SELECT FOR NO KEY UPDATE`
- FOR KEY SHARE
  - 和FOR SHARE相似，但是弱。

PG不再内存中，存储行的修改信息，因此同一时间修改的行数没有限制。因此，锁住一个行会导致磁盘写，select for update标记的row，这些行会开始写磁盘。

#### 页锁

​	PG中，有对于表的数据页的共享/互斥锁，一旦这些行读取或者更改完成后，相应锁就被释放。应用开发者一般不用考虑这个锁。

#### 死锁

​	显式加锁会提高死锁的发生的概率。

​	` This means it is a bad idea for applications to hold transactions open for long periods of time (e.g., while waiting for user input).`

#### 咨询锁

​	当MVCC模型和锁策略不符合时，采用咨询锁。在表中存储一个标记位能够实现同样的功能，但是咨询锁更快，避免表膨胀，会话结束后能够被Pg自动清理。

获得咨询锁：

1. 会话级别：该级别获得的咨询锁，没有事务特征，事务回滚或者取消，之前获得的咨询锁不会被unlock，一个咨询锁可以多次获得，相应的要多次取消。
2. 事务级别：事务级别获得的锁，事务结束后会自动unlock。

两种方式获得的咨询锁，如果锁在了一个识别符上，那么他们也是互相block的。



#### 禁止添加一个列的时候设定一个默认值

这是一个黄金定律：在生产环境中，添加一个列的时候，不要指定一个默认值

添加列，会采用非常激进的锁策略，这会阻塞读写；如果你添加的列带有默认值，PostgreSQL会重写整张表，来对每一行设置默认值，在大表上可能会是几个小时的工作，这样所有查询就会被阻塞，数据库不可用；

+ DO NOT

```SQL
-- 读写都会被阻塞
ALTER TABLE items ADD COLUMN last_update timestamptz DEFAULT now();
```

+ INSTEAD

```SQL
-- select, update, insert, 和 delete 都会阻塞
ALTER TABLE items ADD COLUMN last_update timestamptz;
-- select 和 insert 可行, 当表重写的时候，部分update·和delete会被阻塞
UPDATE items SET last_update = now();
```

+ BETTER

  ```c
  do {
    numRowsUpdated = executeUpdate(
      "UPDATE items SET last_update = ? " +
      "WHERE ctid IN (SELECT ctid FROM items WHERE last_update IS NULL LIMIT 5000)",
      now);
  } while (numRowsUpdate > 0);
  ```

  为了避免阻塞update和delete，可以一小批的更新，这样添加一个新列，减少对用户的影响

#### 理解锁队列，使用lock timeout

每个PostgreSQL中的锁都有一个锁队列。如果一个锁是排他的，事务A占有，事务B获取的时候，就会在锁队列中等待。有趣的是，如果这时候事务C同样要获取该锁，那么它不仅要和A检查冲突性，也要和B检查冲突性，以及队列中其他的事务；

利用以下查询，可以看出哪些数据库上有锁在等待，其中granted列表示有些锁现在还没有被授予；

```sql
select relation::regclass, locktype, mode, granted FROM pg_locks where relation::regclass::text != 'pg_locks';
```

这就意味着，即使你的DDL语句可以很快的执行，但是它可能会在队列中等待很久，直到前面的查询结束。并且该DDL操作会将后续的查询阻塞；

当你在一个表上，执行一个长查询的时候；

DO NOT

```sql
ALTER TABLE items ADD COLUMN last_update timestamptz;
```

INSTEAD

```sql
SET lock_timeout TO '2s'
ALTER TABLE items ADD COLUMN last_update timestamptz;
```

通过设置`lock_timeout`，这个DDL语句如果遇到锁等待，最终会失败，进而后续的查询只会阻塞2s；这样不好的一点就是，alter table 可能会失败，但是你可以重试；并且可以查看pg_stat_activity看看，是不是有慢查询；

#### CREATE INDEX CONCURRENTLY

另一个黄金定律：永远并发的建索引

在一个大数据集上建索引，有可能会花费数小时甚至数天的时间；常规的create index会阻塞所有的写操作；尽管不阻塞select，但是这还是不好的；

NO

```SQL
-- blocks all writes
CREATE INDEX items_value_idx ON items USING GIN (value jsonb_path_ops);
```

INSTEAD

```SQL
-- only blocks other DDL
CREATE INDEX CONCURRENTLY items_value_idx ON items USING GIN (value jsonb_path_ops);
```

并行的创建索引确实有缺点。如果出了问题，它不会回滚，这会留下一个未完成的index；但是不用担心，`DROP INDEX CONCURRENTLY items_value_idx`，重新创建即可。

#### 越晚使用激进锁策略越好

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

#### 添加主键的时候最小化锁阻塞

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

#### 禁止VACUUM FULL

比起这个操作，我们应该调整AUTOVACUUM设置和使用index来提升查询，定时的使用VACUUM，而不是VACUUM FULL

#### 调整命令顺序，避免死锁

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



ref：

[tipsfordeallocks](https://www.citusdata.com/blog/2018/02/22/seven-tips-for-dealing-with-postgres-locks/)
