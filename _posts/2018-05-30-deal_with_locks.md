---
layout: post
title: 
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
---

## 处理锁的7条建议

数据库操作过程中，一些不恰当的操作，会导致数据库发生锁阻塞，影响DB性能；但是，同样的操作，我们可以通过恰当的操作，避免这一问题，如下是一些建议，欢迎补充；

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
