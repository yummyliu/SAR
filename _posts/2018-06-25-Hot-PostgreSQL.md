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

# 第七章 Heap Only Tuple 和 Index-Only Scan
​	本章中介绍两个和index scan有关的特性——heap only tuple和index-only scan

## 7.1 Heap Only Tuple（HOT）

​	在8.3版本中实现的HOT特性，使得更新行的时候可以将新行放在和老行同一个数据页中，从而高效地利用索引和表的数据页；HOT特性减少了没有必要的VACUUM处理。

​	由于在源码的README.HOT中有关于HOT的详细介绍，本章只是简短的介绍HOT。首先，7.1.1节描述了通过没有HOT特性如何更新一行的描述，阐明要解决的问题。接下来，在7.1.2中介绍了HOT做了什么。

### 7.1.1 没有HOT特性时的行更新

​	假设表tbl有两个列：id和data；id是tbl的主键。

```sql
testdb=# \d tbl
                Table "public.tbl"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 id     | integer |           | not null |
 data   | text    |           |          |
Indexes:
    "tbl_pkey" PRIMARY KEY, btree (id)
```
​	表tbl有1000个元组；最后一个元组的id是1000，存储在第五个数据页中。最后一个元组被相应的索引元组引用，索引元组的key是1000，并且tid是`(5,1)`，参考图7.1(a)

​	需要在indexpage中插入一行记录，这会提高insert和vacuum的cost；
![update](/image/fig-7-01.png)

<center>图 7.1 没有HOT的行更新</center>

​	我们考虑一下，没有HOT时，最后一个元组是如何更新的。

```sql
testdb=# UPDATE tbl SET data = 'B' WHERE id = 1000;
```

​	在这个场景中，PostgreSQL不经插入了一个新的表元组，也在索引页中插入了新的索引元组，参考图7.1(b)。索引元组的插入消耗了索引页的空间，并且索引元组的插入和vacuum都是高代价的操作。HOT的目的是减少这个影响。

### 7.1.2 How HOT performs
​	当基于HOT特性更新行时，如果新元组和老元组的在同一个数据页中；那么就不在相应的索引页中插入索引元组，而是分别设置新元组的`t_informask2`的`HEAP_ONLY_TUPLE`和老元组的t_informask2的`HEAP_HOT_UPDATED`，参考图7.2和7.3；
![hot](/image/fig-7-02.png) <center>图 7.2 HOT的行更新</center>
![informask](/image/fig-7-03.png)

<center>图 7.3 HEAP_HOT_UPDATED和HEAP_ONLY_TUPLE 位</center>

​	比如在这个例子中，Tuple_1和Tuple_2分别被设置成`HEAP_HOT_UPDATED`和`HEAP_ONLY_TUPLE`。

​	另外，不管指针调整（pruning）和碎片整理（defragmentation）是否进行了，都会使用`HEAP_HOT_UPDATED`和`HEAP_ONLY_TUPLE`标志。

​	接下来介绍，基于HOT更新一个元组后，PostgreSQL如何在index scan中，访问这个更新的元组，参考图7.4(a)。

当执行完一个HOT UPDATE后，如果还没进行pruning，读取一个tuple如下

![pruning](/image/fig-7-04.png)

<center>图 7.4 行指针调整</center>

1. 找到目标数据元组的索引元组
2. 通过索引元组，找到行指针[1]
3. 读取Tuple_1
4. 通过Tuple_1的t_ctid，读取Tuple_2

​	这样，PostgreSQL会读取两个元组，Tuple_1和Tuple_2。基于第五章中描述并发控制机制判断哪个可见；但是，如果数据页中的过时元组（dead tuple）被清理了，那么就有问题了。比如，在图7.4(a)中，如果Tuple_1由于过时被清理了，Tuple_2就不能通过索引访问了。

​	为了解决这个问题，在恰当的时候，PostgreSQL重建将指向老元组的行指针指向新元组的行指针。在PostgreSQL中，这个过程称为指针调整（pruning）。在图7.4（b）中，介绍了PostgreSQL在pruning之后，如何访问更新的元组。

1. 找到索引元组
2. 通过索引元组，找到行指针[1]
3. 通过重定向的行指针[1]，找到行指针[2]；
4. 通过行指针[2]，读取Tuple_2

可能的话，在SQL命令，比如 `SELECT UPDATE INSERT DELETE`执行的时候，执行一次pruning。因为太复杂了，额外的执行时间在本章中没有描述，细节可以在源码的README.HOT中找到。

在PostgreSQL执行pruning时，可能的话，在恰当的时间，会清理老元组，这称为`defragmentation`，在图7.5中描述了HOT中的defragmentation过程。

![](/image/fig-7-05.png)

<center>图 7.5 整理过时元组的碎片整理</center>

值得注意的是，因为碎片整理并不包括移除索引元组，碎片整理比起常规的vacuum的开销小得多。因此，HOT特性降低了索引和表的空间消耗，最终减少了需要插入的索引元组量和不必要的VACUUM的处理。

> ###### HOT不可用的场景
>
> 为了清晰地了解HOT的工作，这里介绍下HOT不可用的场景。
>
> 1. 当更新的元组和老元组在不同同一个数据页中时，指向这个元组的索引页也会添加一个索引元组，如图7.6(a)所示；
> 2. 当索引的建更新了，会创建一个新的索引元组，如图7.6(b)

![notavaible](/image/fig-7-06.png)

<center>图 7.6 HOT不可用的情况</center>

> `pg_stat_all_tables`视图提供了每个表的统计信息视图

## Index-Only Scan

当SELECT语句的所有的目标列都在index-key中，为了减少I/O代价，index-only scan（又叫index-only access）不访问底层数据表，直接使用索引的键值。所有的商业关系型数据库中都提供这个技术，比如DB2和Oracle。PostgreSQL在9.2版本中引入这个特性。

接下来，基于一个特别的例子，介绍了PostgreSQL中的index-only scan的工作过程。

关于这个例子的解释：

+ 表定义

  我们有一个tbl变，如下

  ```sql
  testdb=# \d tbl
        Table "public.tbl"
   Column |  Type   | Modifiers 
  --------+---------+-----------
   id     | integer | 
   name   | text    | 
   data   | text    | 
  Indexes:
      "tbl_idx" btree (id, name)
  ```

+ 索引

  表tbl有一个索引tbl_idx，包含两个列：id和name

+ 元组

  tbl已经插入了一些元组。

  `id=18 and name = 'Queen'`的Tuple_18存储在第0个数据页中

  `id=19 and name='BOSTON'`的Tuple_19存储在第1个数据页中

+ 可见性

  所有的元组在第0个页中永远可见；第1个页中的元组不总是可见的；每个页的可见性存在相应的`visibility map`中，关于vm的描述参考第六章第二节；

  ​

  我们一起看一下，下面的SELECT语句，在PostgreSQL中，如何读取元组。

```sql
testdb=# SELECT id, name FROM tbl WHERE id BETWEEN 18 and 19;
 id |  name   
----+--------
 18 | Queen
 19 | Boston
(2 rows)
```

​	查询需要从表中读取两列：id和name，然而索引tbl_idx包含了这些列。因此，使用索引扫描时，一开始就认为，没有必要访问表，因为索引中已经包含了必要的数据。但是，原则上，PostgreSQL需要检查元组的可见性，并且索引元组没有关于堆元组的任何事务信息，比如t_xmin和t_xmax，详细参考第五章。因此PostgreSQL需要访问表数据来检查索引元组中数据的可见性。这有点本末倒置。

​	PostgreSQL使用相应表上的vm，来避免这一问题。如果所有页中的存在的元组是可见的，PostgreSQL就使用索引元组，而不用访问索引元组指向的数据页去检查可见性；否则，PostgreSQL读取索引页指向的数据元组，从而检查元组可见性，这就是原来要做的工作。

​	在这个例子中，因为第0页标记为可见的，Tuple_18不需要访问；反而，由于第1页没有标记可见，需要访问Tuple_19，来处理并发控制，如图7.7。

![vm](/image/fig-7-07.png)

<center>图 7.7 Index-Only Scan的工作过程</center>