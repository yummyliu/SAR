---
layout: post
title: (译)Head Only Tuple和Index-Only Scan
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
typora-root-url: ../../yummyliu.github.io
---

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
