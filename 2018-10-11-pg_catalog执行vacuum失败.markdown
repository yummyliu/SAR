---
layout: post
title: 
date: 2018-10-11 11:35
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---
> * TOC
{:toc}



# 现象

![image-20190110130454213](/image/image-20190110130454213.png)

系统表pg_shdescription是cluster共享的catalog（其他共享的catalog还有pg_authid等）。出现的问题如下：

```sql
postgres=# vacuum FREEZE VERBOSE pg_shdescription;
INFO:  vacuuming "pg_catalog.pg_shdescription"
ERROR:  found xmin 552 from before relfrozenxid 2018400456
postgres=# vacuum FREEZE VERBOSE pg_statistic;
INFO:  vacuuming "pg_catalog.pg_statistic"
ERROR:  found xmin 555 from before relfrozenxid 2018400456

postgres=# select relname,relfrozenxid from pg_class where relname ~ 'pg_statistic$|pg_shdescription$';
     relname      | relfrozenxid
------------------+--------------
 pg_statistic     |   2018400456
 pg_shdescription |   2018400456
(2 rows)
```

该错误信息是说明发现了一个比表的冻结年龄更早的数据记录。

> PgSQL的年龄先后判断逻辑，在函数`TransactionIdPrecedes`中。通过debug，改函数确实返回1。
>
> ```c
> Run till exit from #0  TransactionIdPrecedes (id1=555, id2=2018400456) at transam.c:308
> 0x00000000004dc72d in heap_prepare_freeze_tuple (tuple=0x2b9ece599780, relfrozenxid=2018400456, relminmxid=1, cutoff_xid=2423685768,
>     cutoff_multi=4289967297, frz=0x1cf8e58, totally_frozen_p=0x7ffe5082dacf "") at heapam.c:6669
> 6669    heapam.c: No such file or directory.
>         in heapam.c
> Value returned is $5 = 1 '\001'
> ```



理论上，表中不会出现比表的relfrozenxid小的年龄；因为，每次vacuum要将表中需要冻结的tuple，在t_infomask中标记为frozen，并且更新pg_class中的relfrozenxid。这是一次非正常情况，相应在代码中这个错误的err_code为`ERRCODE_DATA_CORRUPTED`。

之前经常在pg_authid中出现这个错误，解决的方法是将有问题的role删除，但是这里删除后需要等待一会，才能正常执行vacuum。这次出问题的表正常只有三条记录，现在有问题的记录是一个不可见的记录，无法进行删除。

# 问题定位

该库是pg_upgrade升级上来的，通过link的方式升级的，并且升级过后没有删除老的数据；从系统表中可以看出从12.7日，改表的vacuum就出现了异常；该时间点就是该库升级的时间点。

```sql
» SELECT last_vacuum from pg_stat_sys_tables where relname ~ 'pg_statistic$|pg_shdescription$';
          last_vacuum
-------------------------------
 2018-12-07 02:18:01.396058+08
 2018-12-07 02:18:01.374234+08
(2 rows)
```

# 解决方案

由于之前出现这种问题，通过删除数据的方式解决，现在没有找到删除数据的方法。决定通过逻辑复制的方式，将业务数据复制出来，而不是直接修复系统表。

留下的有问题的老库，作为一个研究对象，后期进行研究。