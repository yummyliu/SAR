---
layout: post
title: 
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
---

# MVCC&VACUUM

##MVCC

在并发控制中，有两种方式来处理冲突；锁（悲观锁），以及乐观锁（MVCC）；在读多写少的情景下， 使用乐观锁，能够提升系统性能；

通过MVCC，数据带上版本号，避免数据库读写冲突的问题。大概有两种实现的方式，关键就在于将旧数据放在什么地方；

1. 将多版本的记录都存储在数据库中，垃圾收集器清理不需要的记录。PostgreSQL/Firebird/Interbase 使用这种方式，SQL Server将其存储在tempdb中
2. 将最新的记录放在数据库中，通过恢复undo日志，来重建旧版本的数据。在Oracle、MySQL(innodb)中采用这一种方式。

### MVCC in PostgreSQL

在PostgreSQL中，当一个行被更新，创建一个新的tuple插入到表中。old tuple 保留一个指向new tuple的指针。old tuple 标记为 expired,但是仍然保留在数据库中，直到被作为垃圾清理了。

为了支持多版本，每个tuple有额外的数据行：

- xmin: 创建该tuple的事务id，insert/update 这一行
- xmax：删除该tuple的事务id，delete or 在该行上的update，初始为null（0）·

> xid介绍
>
> + 可以表示uint32范围，大概40亿个事务
> + xid的循环空间是一个没有端点的环
> + 对于每个xid，都有大约20亿个新事务，20亿个老事务

事务的状态保存在CLOG/xact中，表中包含两个bit的status信息（in-progress/committed/aborted）。当事务中止，PostgreSQL不做undo操作，只是简单的在clog/xact中将事务标记为aborted。因此PostgreSQL的表可能会包含aborted的事务的数据(为了避免总是查询CLOG，PostgreSQL在tuple中保存了status标记(known commited/known aborted))。

对于MVCC产出的脏数据以及XID回卷的问题，PG中Vacuum进程作为数据库的维护进程，有两个主要任务，来处理这两个个问题：

1. 清理dead tuple ：expired和aborted的行以及这些行相关的索引。
2. 冻结旧事务id，更新相关系统表，移除不需要的clog/xact

#### xid回卷

PostgreSQL的MVCC事务，依赖于xid的比较：由于xid的尺寸（32位）有限，如果数据库长时间运行，那么可能xid不够，回卷到3（0，1，2已作为特殊用途）。当事务id没有发生回卷操作的时候，简单的比较大小就行。但是如果xid回卷，那么如何比较xid的新旧呢？

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

函数中，第一个判断条件`(!TransactionIdIsNormal(id1) || !TransactionIdIsNormal(id2))`中，如果有一个事务是特殊事务，那么特殊事务一定比普通事务旧（等价于无符号的比较）；

对于普通事务，PostgreSQL中是使用2^31取模的方法来比较事务的新旧的。也就是在PostgreSQL中，认为同一个数据库中，存在的最旧和最新两个事务之间的年龄是最多是2^31，而不是无符号整数的最大范围2^32，只有2^32的一半。（当id2比id1新时，意味着`0<id2-id1<2^31 <=> -2^31<id1-id2<0`,转成有符号数就是负数，代码中，利用这个小技巧判断）。

所以，当数据库中的表的年龄差，要超过2^31次方时，就把旧的事务换成一个**事务ID为2的特殊事务**，这就frozen操作。在VACUUM中，会将行frozen，被frozen的行，相当于非常久远的行，比所有事务都旧；

> 在PostgreSQL中的三个特殊事务ID:
>
> 1. InvalidTransactionId = 0：表示是无效的事务ID
> 2. BootstrapTransactionId = 1：表示系统表初使化时的事务ID，比任务普通的事务ID都旧。
> 3. FrozenTransactionId = 2：冻结的事务ID，比任务普通的事务ID都旧。

#### vacuum操作的两种模式

![anti](/image/anti-wa.jpeg)

PostgreSQL的Vacuum操作，通过判断visibility map，有时候会跳过需要frozen的页，anti-wraparound vacuum（也叫 eager vacuum）就是强制对全表进行frozen操作；

在Postgresql.conf中，配置有三个参数：

+ `vacuum_freeze_table_age`
+ `autovacuum_max_freeze_age`
+ `vacuum_freeze_min_age`

每个表都有一个`pg_class.relfrozenxid`值(整个数据库有一个`pg_database.datfrozenxid`，是`pg_class.relfrozenxid`中最小的)。

将`current_xid`(当前所有的运行事务的xid最老的，如果只有VACUUM那么就是是vacuum事务的xid)和这三个参数的比较，来执行对应的vacuum操作：

| `current_xid<relfrozenedxid+vacuum_freeze_table_age`         | lazy vacuum                                   |
| ------------------------------------------------------------ | --------------------------------------------- |
| `current_xid>relfrozenedxid+vacuum_freeze_table_age` && `current_xid<relfrozenedxid+autovacuum_max_freeze_age` | 如果执行VACUUM,那么执行anti-wraparound vacuum |
| `current_xid>relfrozenedxid+vacuum_freeze_table_age`         | 强制执行anti-wraparound vacuum                |

执行anti-wraparound vacuum过后，relfrozenxid=vacuum_freeze_min_age;

##### lazy mode 和 anti-wraparound vacuum

lazy vacuum执行的时候，会通过visible map，判断page中是否有垃圾需要清理；所以在lazy mode的时候，可能不会完全冻结表中的元组。

anti-wraparound vacuum，会扫描所有的page，由于没有相应VM机制，执行比较慢；在pg9.6中，visible map加了一个bit位：all-frozen，提高anti-wraparound vacuum的性能。

| case                                                         | 是否可见               |
| ------------------------------------------------------------ | ---------------------- |
| 当前事务ID>xmin && (当前事务ID<xmax或者xmax=0）              | 该数据行对当前事务可见 |
| 数据行frozen（9.4之前是将xmin设置成特殊的FrozenTransactionId） | 该数据行对当前事务可见 |

### VACUUM in PostgreSQL

由于PG的mvcc机制会将expired以及aborted的tuple留在堆表中，这会导致表以及索引的膨胀，对于这个问题，PG通过vacuum机制来处理。

从8.3开始，autovacuum默认可用，并且是多进程的架构，这样同时可以不止一个表的vacuum。并且可以自己决定该对哪些表进行vacuum清理，这可以减少大部分的手动vacuum的工作；

在8.4中，提出了两个特性；

1. 动态分配空间的free space map，这样当fsm空间不够，PG不会丢失对这些的记录；
2. 添加一个visibility map，维护了堆表中哪些page只包含所有事务都可见的tuple，VACUUM可以通过判断该map，跳过部分没有脏row的page。

#### VACUUM卡主的问题

PG中的锁，除了用户可见的表锁，行锁之外，还有内部的页锁。VACUUM被卡主这个问题主要是因为 autovacuum等待的页锁，很长时间没有释放，这样就会导致膨胀问题。vacuum被卡主，主要是由于以下两个原因：

1. VACUUM请求较重的锁

   在pg_locks中，可以看到ShareUpdateExclusiveLock；一般来说，可能有另一个人在执行VACUUM/CLUSTER/ALTER TABLE，或者直接LOCK table了。

2. VACUUM请求表中page的清理lock；

   在pg_lock中看不到这个锁，vacuum进程sleep了，没有任何IO和cpu。这会在VACUUM过程中的任何时间发生。这主要是当时的数据库查询，需要请求相应的要清理的page，主要是由于防止vacuum清理的要查询的page，所以锁住了。一个经典的情况就是，查询执行过程中，停止了，比如使用了。

#### 针对VACUUM卡主问题的优化

在9.1中，autovacuum可以跳过当前获得不到表锁的表。

在9.2中，系统可以跳过获得不了清理锁的相应page，除非这个page包含一些必须要remove或者frozen的tuple。

在9.5中，减少了b-index保留最近访问的index page的情形，这样减少了VACUUM因为等待index scan而被卡主的case。

#### 针对减少page访问次数的优化

在9.3之前，在上一次VACUUM中，某个page如果没有更改，那么这次的vacuum就将其中的tuple标记为all-visible，这样，除非xid溢出，之后的VACUUM就会跳过这一个page。

在9.6中，即使xid溢出，VACUUM也可能跳过这个page。在9.6中的visibility map，增加了一个bit位，不仅记录page是不是all-visible，也记录是不是all-frozen.

#### VACUUM依然存在的问题

1. 如何避免对index page的无谓扫描。比如，即使VACUUM发现没有dead row，依然会扫描index page，回收empty page。[improve](https://www.postgresql.org/message-id/CAD21AoAX+d2oD_nrd9O2YkpzHaFr=uQeGr9s1rKC3O4ENc568g@mail.gmail.com) 针对这一问题有一个patch，但是对于b-index与xid溢出的处理没有处理好。
2. 表中有deadrow但是很少的情况下的VACUUM；autovacuum不会被触发，而VACUUM需要全表扫描有太耗时。
3. VACUUM需要好多操作，将很多操作放在一起执行是很有效率，但是一次执行需要的时间就很多，是不是可以考虑将VACUUM操作分成几小步来操作。
4. 对于大的DB，默认的Cost-based Vacuum Delay设置太小了，取决于表脏的程度，VACUUM的速度不同，有可能下一个VACUUM准备执行的时候，上一个VACUUM还没有结束，那么这个表就会bloat。
5. VACUUM无法感知系统的负载情况，只有人为的配置，在空闲的时候执行VACUUM。

### VACUUM总结

VACUUM发展了这么久，但是依然需要监控和管理。小型系统可以应付，大系统需要良好的 监控、管理、调优。获取未来的release会有其他的提升。

[vacuum的发展](http://rhaas.blogspot.co.uk/2018/01/the-state-of-vacuum.html)

[Vacuum Processing](http://www.interdb.jp/pg/pgsql06.html)

[关于其他数据库具体的MVCC细节](http://amitkapila16.blogspot.co.uk/2015/03/different-approaches-for-mvcc-used-in.html)
