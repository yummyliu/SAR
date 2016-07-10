---
layout: post
title: postgresql各种scan
date: 2016-06-17 14:14
categories: jekyll update
---

### 概述

说起scan，应该是postgresql中最简单的一个操作元组了，无非就是和读取源数据信息，但是数据性能的好坏就是和磁盘IO相关，scan是必须要用到磁盘IO的，而上次的join，sort等操作符可能会用到额外用到IO，所以在scan这里做好scan的查询计算的选择是很重要的。会用到到一些表的数据统计信息来，选择执行计划，是否使用索引，有时候使用索引并不见得就好，虽然索引扫描，能够读取索引块直接定位到元素的位置，但是每次至少两次磁盘IO，而且这两次磁盘IO，可能不是顺序的。

#### seq scan

首先是最常见的seq scan，**顺序扫描**表文件，找出符合条件的元组。

#### index scan

利用索引直接定位，来查找

#### bitmap heap scan

同样是利用索引

> A plain indexscan fetches one tuple-pointer at a time from the index,
> and immediately visits that tuple in the table.  A bitmap scan fetches
> all the tuple-pointers from the index in one go, sorts them using an
> in-memory "bitmap" data structure, and then visits the table tuples in
> physical tuple-location order.  The bitmap scan improves locality of
> reference to the table at the cost of more bookkeeping overhead to
> manage the "bitmap" data structure --- and at the cost that the data
> is no longer retrieved in index order, which doesn't  matter for your
> query but would matter if you said ORDER BY. 

普通的索引扫描（ index scan） 一次只读一条索引项，那么一个 PAGE面有可能被多次访问；而 bitmap scan 一次性将满足条件的索引项全部取出，并在内存中进行排序, 然后根据取出的索引项访问表数据，如下

```
   ->  Bitmap Heap Scan on public.tb_index_test  (cost=11.38..71.39 rows=400 width=0) (actual time=0.062..0.120 rows=399 loops=1)
		Output: id, name
        Recheck Cond: (tb_index_test.id < 400)
        Buffers: shared hit=6
        
        ->  Bitmap Index Scan on tb_index_test_pkey  (c<span style="color:#ff0000;">ost=0.00</span>..11.29 rows=400 width=0) (actual time=0.047..0.047 rows=399 loops=1)
               Index Cond: (tb_index_test.id < 400)
               Buffers: shared hit=3
```