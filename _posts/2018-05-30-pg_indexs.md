---
layout: post
title: 
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PG
---

数据库的索引是提高性能的关键。如果没有索引，scan只能seq scan; pg中常用的索引有 B-tree, Hash, GiST, SP-GiST, GIN 和 BRIN;

## 测试数据

```sql
You are now connected to database "demo" as user "postgres".
demo=# create table sample (x numeric);
CREATE TABLE
demo=# INSERT INTO sample select random() * 10000 from generate_series(1,10000000);
INSERT 0 10000000
demo=# create index id_x on sample(x);
CREATE INDEX
demo=# analyze ;
ANALYZE

```

## B-tree

![btree](/image/btree.svg)

数据库中的类B-tree索引，多路平衡搜索树，树高低，大概两三层就够了，减少磁盘IO；

### PG中的B-tree

#### 固定大小的node

PG中index节点大小就是内存的一个page；大部分Btree理论中，每个node上有固定数目的keys；然而，在PG中，每个node是有固定大小的Bytes；所以，branching factor依赖于存储在index中独立tuple的大小；如果tuple大小的可变的,那么每个node的子node数目会不同；一般来说，branching factor是在几百到几千之间。

+ 较小的索引建可以比较大的索引键执行得更好，因为它们将允许更高的分支因子；
+ B树种插入数据，会定期的split；一般来说是，每个bucket超过一定数量就split；在PG中，nodessplite产生新的相同bytes的数据；
+ 超大的数据不会存储在索引中，会用TOAST技术来存储；

#### 并发

类似一些别的计算机中的技术，理论到实际应用会做一些调整，在PG中B树使用 [Lehman and Yao](https://github.com/postgres/postgres/blob/master/src/backend/access/nbtree/README) 的方法；

> 比起经典的B树，L&Y中的每个节点，添加了一个指向右边兄弟的连接；并且在每个节点上添加了一个high key(当前节点key的上限)；这两个特性，帮助PG获知当前的并行节点分裂，这样不会阻塞用户读（除非修改一个正在读的page）；
>
> 通过high key，定位tuple是否在这个node上，如果大于highkey，通过right link，在兄弟节点上找；

实际中，如果不是读写同一个page，就不会有并发冲突；

#### 选择少量数据

```sql
demo=# explain select * from sample where x = 42353;
                               QUERY PLAN
-------------------------------------------------------------------------
 Index Only Scan using id_x on sample  (cost=0.43..8.45 rows=1 width=11)
   Index Cond: (x = '42353'::numeric)
(2 rows)
```

如果只选择一小部分行，PG可以直接查询index；这种情况下，会用“Index Only Scan”，因为所有的列都已经在index中了。

#### 选择大量数据

```sql
demo=# explain select * from sample where x < 42353;
                            QUERY PLAN
-------------------------------------------------------------------
 Seq Scan on sample  (cost=0.00..179055.00 rows=10000000 width=11)
   Filter: (x < '42353'::numeric)
(2 rows)
```

Seq Scan并不总是差的；当从统计信息中判断，选出的row很多时，PG会从选择seq scan；

#### 选择中量数据

```sql
demo=# explain select * from sample where x < 423;
                                QUERY PLAN
---------------------------------------------------------------------------
 Bitmap Heap Scan on sample  (cost=9601.76..68839.53 rows=414622 width=11)
   Recheck Cond: (x < '423'::numeric)
   ->  Bitmap Index Scan on id_x  (cost=0.00..9498.10 rows=414622 width=0)
         Index Cond: (x < '423'::numeric)
(4 rows)
```

在bitmap Index scan中，每个block在扫描的时候只用到一次。并且表上有多个索引的时候，同样奏效；首先扫描index，找到相应的blocks,然后根据找到blocks，真正去取数据;

### Hash index

字段中存储的是hash值，只能进行等值查询；适用于字段值较长的列，在PG10中，进行了并行增强；

### Brin

块级别的索引，记录块中的统计信息；占用空间小，对数据写入跟新影响很小；

### GiST

通用搜索树，适用于多维数据类型和集合数据类型；基于B树的多列索引，查询的时候第一个查询条件必须是建立索引的第一列，Gist没有这个限制；但是建索引耗时，查询性能也不性，好在通用；

### sp-GIST

使用了空间分区的方法，使得SP-GiST可以更好的支持非平衡数据结构；

### gin

Generalized Inverted Index倒排索引，`(‘hello', '14:2 23:4')`，key=hello出现在14块的2bit，23块的4bit；

NOTE：gin 和 gist中都需要用到一些[特殊搜索符号](https://www.postgresql.org/docs/current/static/functions-geometry.html)；`&&`,`@>`等；

### bloom

支持任意列组合的等值查询，用于收敛结果集（排除绝对不满足条件的结果，剩余的结果再挑选满足条件的）；

## Heap Only Tuple
有这样一张表, 执行一条更新语句：
```
testdb=# \d tbl
                Table "public.tbl"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 id     | integer |           | not null |
 data   | text    |           |          |
Indexes:
    "tbl_pkey" PRIMARY KEY, btree (id)

testdb=# UPDATE tbl SET data = 'B' WHERE id = 1000;
```
需要在indexpage中插入一行记录，这会提高insert和vacuum的cost；
![update](/image/fig-7-01.png)

### How HOT performs
当更新一个行的时候，如果new tuple和old tuple的在同一个page中；那么在Indexpage中，就不插入记录，
而是更新分别更新newtuple和oldtuple的t_informask2字段为HEAP_HOT_UPDATED和HEAP_ONLY_TUPLE;
![hot](/image/fig-7-02.png)
![informask](/image/fig-7-03.png)

当执行完一个HOT UPDATE后，如果还没进行pruning，读取一个tuple如下

![pruning](/image/fig-7-04.png)

1. 找到target tuple的index tuple
2. 通过point(1) 找到indextuple
3. 读取tuple1
4. 通过tuple1的t_ctid读取tuple2

这样PG会读取两个tuple，通过mvcc判断哪个可见；但是如果tuple1被remove了，那么tuple2就找不到了；为了解决这个问题PG在恰当的时候（当SQL执行SELECT UPDATE INSERT DELETE的时候），执行一次pruning，将指向oldtuple的pointer指向newtuple的pointer，如上，并且在执行pruning的时候，可能会remove old tuple，这称为defragment。defragment比起vacuum的开销小得多，因为其并不包括移除indextuple。

因此，HOT UPDATE降低了index page和table page的消耗，并且减少了VACUUM的处理tuple数。

### Case HOT not available

1. new updated tuple 不在同一个page中；
2. index tuple的key被更新了的时候；

![notavaible](/image/fig-7-06.png)



## Index Only Scan

查询需要的列都在index中存在了，那么可以只访问indextuple即可得到结果；但是indextuple中没有储存t_xmin t_xmax；所以还是需要访问heap tuple来得到visibility信息；这有点别扭，但是现在通过visibility map可以一定程度避免这个问题；比如下图，tuple_18通过vm判断是可见的，但是tuple_19通过vm判断不了，继续访问head_tuple

![vm](/image/fig-7-07.png)

[ref](https://rcoh.me/posts/postgres-indexes-under-the-hood/)

[ref](http://www.interdb.jp/pg/pgsql07.html)
