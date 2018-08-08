---
layout: post
title: 认识PostgreSQL的MVCC
subtitle: PostgreSQL中，对DML语句使用快照隔离，对DDL语句使用2PL，本文中主要介绍PostgreSQL的快照隔离，即MVCC；
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
typora-root-url: ../../yummyliu.github.io
---

> * TOC
> {:toc}

当数据库中并发的执行多个任务时，并发控制是维护事务的一致性和隔离性的机制。

有三个广义上的并发控制策略：多版本并发控制（MVCC），严格两阶段锁（S2PL），和乐观的并发控制。每个技术有多个变种。MVCC的主要优势是“读操作不阻塞写操作，并且写操作不堵塞读操作”；而在一个基于S2PL的系统中，因为写会在数据项上获取一个排他锁，因此写一个数据项必须阻塞读。PostgreSQL和其他一些使用变种MVCC的RDBMS，称之为快照隔离（SI）；

通常MVCC的是实现方式，大概有两种，关键就在于将旧数据放在什么地方；

 	1. 将多版本的记录都存储在数据库中，垃圾收集器清理不需要的记录。PostgreSQL/Firebird/Interbase 使用这种方式，SQL Server将其存储在tempdb中；
		2. 将最新的记录放在数据库中，通过恢复undo日志，来重建旧版本的数据。在Oracle、MySQL(innodb)中采用这一种方式。

SI不允许出现在ANSI SQL-92标准中定义的三种异常，即脏读，不可重复读和幻读。但是，SI不能实现真正的可串行化，因为它允许**序列化异常**，比如 *Write Skew* 和 *Read-only Transaction Skew*。为了解决这个问题，从版本9.1开始添加了可序列化快照隔离（SSI）。 SSI可以检测序列化异常，并且可以解决由这种异常引起的冲突。因此，PostgreSQL 9.1及更高版本提供了真正的SERIALIZABLE隔离级别。 （另外，SQL Server也使用SSI，Oracle仍然只使用SI。）

## PostgreSQL中基于MVCC的并发控制

​	在基于MVCC的DML操作中，当一个行被更新，创建一个新的tuple插入到表中。老tuple保留一个指向新tuple的指针。老tuple 标记为`expired`，但是仍然保留在数据库中，直到被作为垃圾清理了。

##### 多版本的元组如何区分？

区别新老版本的依据是事务ID，为了支持多版本，每个tuple有额外的xid列：

```c
typedef struct HeapTupleFields
{
	TransactionId t_xmin;		/* inserting xact ID */
	TransactionId t_xmax;		/* deleting or locking xact ID */

	union
	{
		CommandId	t_cid;		/* inserting or deleting command ID, or both */
		TransactionId t_xvac;	/* old-style VACUUM FULL xact ID */
	}			t_field3;
} HeapTupleFields;
struct HeapTupleHeaderData
{
	union
	{
		HeapTupleFields t_heap;
		DatumTupleFields t_datum;
	}			t_choice;

	ItemPointerData t_ctid;		/* current TID of this or newer tuple (or a
								 * speculative insertion token) */

	/* Fields below here must match MinimalTupleData! */

	uint16		t_infomask2;	/* number of attributes + various flags */

	uint16		t_infomask;		/* various flag bits, see below */

	uint8		t_hoff;			/* sizeof header incl. bitmap, padding */

	/* ^ - 23 bytes - ^ */

	bits8		t_bits[FLEXIBLE_ARRAY_MEMBER];	/* bitmap of NULLs */

	/* MORE DATA FOLLOWS AT END OF STRUCT */
};
```

- xmin: 创建该tuple的事务id；
- xmax：删除该tuple的事务id或者对该元组加锁的事务ID，初始为null（0）；

##### 事务ID

事务ID是一个uint32类型的整数，由于尺寸（32位）有限，如果数据库长时间运行，那么可能xid不够，回卷到3（0，1，2已作为特殊用途）。

> 在PostgreSQL中的三个特殊事务ID:
>
> 1. InvalidTransactionId = 0：表示是无效的事务ID
> 2. BootstrapTransactionId = 1：表示系统表初使化时的事务ID，比任务普通的事务ID都旧。
> 3. FrozenTransactionId = 2：冻结的事务ID，比任务普通的事务ID都旧。

基于每个tuple上的事务ID，当事务ID没有发生回卷操作的时候，按照如下函数比较事务的新旧。

```c
// src/backend/access/transam/transam.c
/*
 * TransactionIdPrecedesOrEquals --- is id1 logically <= id2?
 */
bool
TransactionIdPrecedesOrEquals(TransactionId id1, TransactionId id2)
{
    int32       diff;

    if (!TransactionIdIsNormal(id1) || !TransactionIdIsNormal(id2))
        return (id1 <= id2);

    diff = (int32) (id1 - id2);
    return (diff <= 0);
}
```

​	函数中，第一个判断条件`(!TransactionIdIsNormal(id1) || !TransactionIdIsNormal(id2))`中，如果有一个事务是特殊事务，那么特殊事务一定比普通事务旧（等价于无符号的比较）；

​	对于普通事务，PostgreSQL中是使用2^31取模的方法来比较事务的新旧的。也就是在PostgreSQL中，认为同一个数据库中，存在的最旧和最新两个事务之间的年龄是最多是2^31，而不是无符号整数的最大范围2^32，只有2^32的一半。

> 小技巧：当id2比id1新时，意味着`0<id2-id1<2^31 <=> -2^31<id1-id2<0`，转成有符号数就是负数

​	所以，当数据库中的表的年龄差，要超过2^31次方时，就需要把旧的事务换成一个**事务ID为2的特殊事务**，这就是frozen操作。在VACUUM中，会将行frozen，被frozen的行，相当于非常久远的行，比所有事务都旧；

##### snapshot

PostgreSQL中的快照记录了当时事务的状态，

```sql
testdb=# SELECT txid_current_snapshot();
 txid_current_snapshot 
-----------------------
 100:104:100,102
(1 row)
```

**xmin:xmax:xip_list**：当前时间点下，最老的事务xmin、最新的事务、以及还没完成事务列表xip_list

![](/image/fig-5-08.png)

+ `100:100`：txid<=99的不活跃了，txid>=100还活跃；
+ `100:104:100,102`：txid<=99的不活跃了，txid>=104还活跃，100,102是活跃的，101，103不活跃了

在RC级别，事务中的每个语句执行的时候，获取一个`txid_current_snapshot`；其他级别在事务一开始的时候获取`txid_current_snapshot`；这个用来比较可见性；

##### 隔离性

![](/image/fig-5-09.png)

事务管理器一直维护了事务的状态；如上图有三个事务ABC，分别是RC/RC/RR隔离级别；当在时间点T5中，B和C中执行select时，由于RC重新获取新的snapshot，此时A提交的数据对B是可见的，但是C还是不可见；因此在RC级别中，某一行的数据在前后可能不一致，那么就不能重复读了；

可见，在PostgreSQL中判断可见性是基于三个信息判断：

+ t_xmin/t_xmax
+ clog
+ 从事务管理器获取的snapshot

> | 隔离级别        | 脏读 | 不可重复读 | 幻读                       | 序列化异常 |
> | --------------- | ---- | ---------- | -------------------------- | ---------- |
> | READ COMMITTED  | N    | P          | P                          | P          |
> | REPEATABLE READ | N    | N          | N in PG; but P in ANSI SQL | P          |
> | SERIALIZABLE    | N    | N          | N                          | N          |
>
> 在PostgreSQL的RR级别中，当事务开始后，任何操作就是基于最开始的快照；因此，幻读也不会发生（幻读的发生也是当前事务能够感知到别的事务在其开始之后的更改）

## PostgreSQL的VACUUM操作

​	事务的状态保存在clog中，表中包含两个bit的status信息（`in-progress/committed/aborted`）。当事务中止，PostgreSQL不做undo操作，只是简单的在clog/xact中将事务标记为aborted。因此PostgreSQL的表可能会包含aborted的事务的数据（为了避免总是查询CLOG，PostgreSQL在tuple中保存了status标记`known commited/known aborted`）。

​	对于MVCC产出的脏数据以及XID回卷的问题，Vacuum进程作为数据库的维护进程，有两个主要任务，来处理这两个个问题：

1. 清理dead tuple ：expired和aborted的行以及这些行相关的索引。
2. 冻结旧事务id，更新相关系统表，**移除不需要的clog/xact**

> vacuum和analyze是两个操作，但是vacuum命令可以加一个可选参数analyze，表示执行vacuum的时候收集一下统计信息；但是freeze不是一个单独的命令，执行vacuum不加freeze的时候也会，执行一些冻结操作

#### 手动和自动vacuum	

我们可以手动或者自动的进行vacuum操作，VACUUM 操作会重新回收dead tuple的磁盘空间，但是不会交给OS，而是为新tuple留着；

> VACUUM FULL 会把空间返回给OS，但是会有一些弊端；
>
> 1. 排他的锁表，block all ops
> 2. 会创建一个表的副本，所以会将使用的磁盘空间加倍，如果磁盘空间不足，不要执行

​	VACUUM是手动执行的，虽然可以利用定时任务周期执行，但是周期的大小不确定，而且有可能这个周期内并没有dead tuple产生，这样就徒增CPU和IO的负载；

​	`autovacuum`是按需执行的；autovacuum间隔`autovacuum_naptime`执行一次`vacuum`和`analyze`命令；但是，为了避免autovacuum对系统的影响，如果autovacuum的代价超过了`autovacuum_vacuum_cost_limit`，那么就等`autovacuum_vacuum_cost_delay`这些时间；

>  autoanalyze：autovacuum 除了干回收的事，还同时统计信息；

#### vacuum处理的两种模式

+ lazy vacuum执行的时候，会通过visible map，判断page中是否有垃圾需要清理；所以在lazy mode的时候，可能不会完全冻结表中的元组。
+ anti-wraparound vacuum，会扫描所有的page，由于没有相应VM机制，执行比较慢；在pg9.6中，visible map加了一个bit位：all-frozen，提高anti-wraparound vacuum的性能。

		每个表都有一个`pg_class.relfrozenxid`值（整个数据库有一个`pg_database.datfrozenxid`，是`pg_class.relfrozenxid`中最小的）。通过比较当前xid（当前所有的运行事务的xid最老的，如果只有VACUUM那么就是是vacuum事务的xid），relfrozenxid与配置有三个参数，决定你发起的vacuum是在那种模式下工作：

		表的`relfrozenxid`超过 `vacuum_freeze_min_age`时，vacuum就要进行freeze的操作，freeze的时候可以通过vm中的信息，来跳过一些page；而当relfrozenxid超`vacuum_freeze_table_age`时，执行vacuum的时候，就不能跳过page，必须全表freeze；而当达到`autovacuum_freeze_max_age`，为了避免xid回卷，autovacuum freeze会强制执行；

![anti](/image/anti-wa.jpeg)

| 阶段                                                         | 操作                                          |
| ------------------------------------------------------------ | --------------------------------------------- |
| `current_xid<relfrozenedxid+vacuum_freeze_table_age`         | 如果执行VACUUM，lazy vacuum                   |
| `current_xid>relfrozenedxid+vacuum_freeze_table_age` && `current_xid<relfrozenedxid+autovacuum_max_freeze_age` | 如果执行VACUUM,那么执行anti-wraparound vacuum |
| `current_xid>relfrozenedxid+vacuum_freeze_table_age`         | 强制执行anti-wraparound vacuum                |

执行anti-wraparound vacuum过后，`relfrozenxid=vacuum_freeze_min_age`;

#### PostgreSQL的VACUUM机制的历史

从8.3开始，autovacuum默认可用，并且是多进程的架构，这样同时可以不止一个表的vacuum。并且可以自己决定该对哪些表进行vacuum清理，这可以减少大部分的手动vacuum的工作；

在8.4中，提出了两个特性；

1. 动态分配空间的free space map，这样当fsm空间不够，PG不会丢失对这些的记录；
2. 添加一个visibility map，维护了堆表中哪些page只包含所有事务都可见的tuple，VACUUM可以通过判断该map，跳过部分没有脏row的page。

##### VACUUM问题的不断优化

PostgreSQL中的锁，除了用户可见的表锁，行锁之外，还有内部的页锁。VACUUM被卡主这个问题主要是因为autovacuum等待的页锁，很长时间没有释放，这样就会导致膨胀问题。

###### 针对VACUUM阻塞问题的优化

在9.1中，autovacuum可以跳过当前获得不到表锁的表。

在9.2中，系统可以跳过获得不了清理锁的相应page，除非这个page包含一些必须要remove或者frozen的tuple。

在9.5中，减少了b-index保留最近访问的index page的情形，这样减少了VACUUM因为等待index scan而被卡主的case。

###### 针对减少page访问次数的优化

在9.3之前，在上一次VACUUM中，某个page如果没有更改，那么这次的vacuum就将其中的tuple标记为all-visible，这样，除非xid溢出，之后的VACUUM就会跳过这一个page。

在9.6中，即使xid溢出，VACUUM也可能跳过这个page。在9.6中的visibility map，增加了一个bit位，不仅记录page是不是all-visible，也记录是不是all-frozen.

##### VACUUM依然存在的问题

1. 如何避免对index page的无谓扫描。比如，即使VACUUM发现没有dead row，依然会扫描index page，回收empty page。[improve](https://www.postgresql.org/message-id/CAD21AoAX+d2oD_nrd9O2YkpzHaFr=uQeGr9s1rKC3O4ENc568g@mail.gmail.com) 针对这一问题有一个patch，但是对于b-index与xid溢出的处理没有处理好。
2. 表中有deadrow但是很少的情况下的VACUUM；autovacuum不会被触发，而VACUUM需要全表扫描又太耗时。
3. VACUUM需要好多操作，将很多操作放在一起执行是很有效率，但是一次执行需要的时间就很多，是不是可以考虑将VACUUM操作分成几小步来操作。
4. 默认的基于代价的VacuumDelay设置太小了，对于大的DB，取决于autovacuum的执行速度，取决于表脏的程度。某些经常更新的表的变脏的速度快于autovacuum清理的速度，表就会膨胀。
5. VACUUM无法感知系统的负载情况，只有人为的配置，在空闲的时候执行VACUUM。

#### VACUUM监控和调优

VACUUM发展了这么久，但是依然需要监控和管理。小型系统可以应付，大系统需要良好的 监控、管理、调优。

##### 监控

- 表中dead tuple数量：`pg_stat_all_tables.n_dead_tup`
- dead/live比例：`(n_dead_tup / n_live_tup)`
- 每行的空间：`select relname,pg_class.relpages / pg_class.reltuples from pg_class where reltuples!=0;`
- 插件：pgstattuple

##### 调优

###### 目标

- 清理dead tuple；不浪费磁盘空间、预防索引膨胀、保证查询响应速度
- 最小化清理的影响；不要清理的太频繁，这样浪费CPU IO资源，影响性能

###### 参数：阈值和膨胀系数

控制autovacuum触发的时间；

- `autovacuum_vacuum_threshold=50`
- `autovacuum_vacuum_scale_factor=0.2`

当`pg_stat_all_tables.n_dead_tup` 超过 `threshold + pg_class.reltuples * scale_factor`就会触发autovacuum；表膨胀超过20%就触发，threshold=50是防止一些小表被频繁的触发；

```sql
ALTER TABLE t SET (autovacuum_vacuum_scale_factor = 0.1);
ALTER TABLE t SET (autovacuum_vacuum_threshold = 10000);
```

###### 节流阀

为了不影响用户使用，不过多的占用cpu io；

清理的进程从disk中逐个的读取page（8k），判断其中有没有dead tuple，如果没有那么就不管了；如果有，就将其中的dead tuple清理掉，然后标记为dirty，最后写出去；这一过程的cost基于一下三个参数来确定，这样我们可以评估`autovacuum`的代价;

```
vacuum_cost_page_hit = 1
vacuum_cost_page_miss = 10
vacuum_cost_page_dirty = 20
```

如果page是从share buffer中读取的，代价为1；如果share buffer中没有，代价为10；如果被清理进程标记为dirty，代价为20；

基于以上的代价，每次autovacuum执行有一个cost的限制；如下

```
autovacuum_vacuum_cost_delay = 20ms
autovacuum_vacuum_cost_limit = 200
```

默认是200，执行代价总数为200的工作；每次工作完，间歇20ms；

那么实际上执行的多少工作？基于20ms的间隔，cleanup能够每秒做50轮；每轮能够做200cost；那么：

- 从shared_buffer读page，80MB/s 
- 从OS（可能是磁盘）读page，8MB/s
- 4Mb/s的速度写出autovacuum标记的脏页

基于当前的硬件，这些参数都太小了，cost_limit可以设置成1000+

###### 工作进程数

db可以启动`autovacuum_max_workers`的进程来清理不同的数据库/表；大表小表的代价不同，这样不会因为大表的工作，阻塞了小表；但是上面的cost_limit是被所有的worker共享的，所以开多个worker也不一定快。

所以，当清理进程跟不上用户活动时，提高worker数是不行的，要提高改变的cost参数；

###### 每个表的节流阀

上面说到costlimit是全局的，被所有worker共享的；这里其实可以在单独的表上设置这两个值：

```sql
ALTER TABLE t SET (autovacuum_vacuum_cost_limit = 1000);
ALTER TABLE t SET (autovacuum_vacuum_cost_delay = 10);
```

这些表的清理工作的cost，不包含在global中；单独计算；提高了一定的灵活性；实际生产中，我们几乎不会用这个特性：

1. 一般需要一个唯一的全局后台清理cost限制
2. 使用多个worker，有时被一起限制，有时单独来，很难监控和分析；

##### 总结

1. 不要禁用autovacuum
2. 在update delete频繁的业务中，减低scale factor，这样清理进程可以及时的进行
3. 在好的硬件下，提高限流阀，这样清理进程不会被中断
4. 单独提高autovacuum_max_worker不行，需要和参数一起调整
5. 使用alter table设置参数要慎重，这会让系统变得复杂；

[vacuum的发展](http://rhaas.blogspot.co.uk/2018/01/the-state-of-vacuum.html)

[Vacuum Processing](http://www.interdb.jp/pg/pgsql06.html)

[关于其他数据库具体的MVCC细节](http://amitkapila16.blogspot.co.uk/2015/03/different-approaches-for-mvcc-used-in.html)

[PostgreSQL的并发控制](http://www.interdb.jp/pg/pgsql05.html)

[vacuum监控](https://blog.2ndquadrant.com/autovacuum-tuning-basics/#PostgreSQL Performance Tuning)
