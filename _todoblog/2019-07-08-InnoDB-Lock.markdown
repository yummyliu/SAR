---
layout: post
title: MySQL的Lock剖析
date: 2019-07-08 10:07
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - MySQL
typora-root-url: ../../yummyliu.github.io
---
* TOC
{:toc}
# MySQL锁机制简述

## 显式事务锁

### 表锁

在MySQL中，有表锁和行锁；在DML中，一般就是行锁，默认的存储引擎InnoDB实现的就是行锁，有X/S两种模式（5.7中加了SX模式）。

当我们要对某个page中的一行记录进行锁定时，需要对上层的table加意向锁——IS/IX，意为该事务中有意向对表中的某些行加X、S锁。意向锁是InnoDB存储引擎自己维护的，用户无法手动添加意向锁。

通过阅读代码，可以看出执行每次操作MySQL上层直接发起`MySQL_lock_table->Innodb::external_lock(F_WRLCK/F_RDLCK)`。结束之后再`MySQL_unlock_table->Innodb::external_lock(F_UNLCK) `。其中模式只有三种（直接使用的Linux文件操作的宏定义）如下：

```c
#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2
// 这是linux头文件中的定义；但是在my_global.h中，是
#define F_RDLCK 1
#define F_WRLCK 2
#define F_UNLCK 3
// 注意区分
```

注意意向锁是表级别的锁（其实就是在整个一级索引上加index->lock），其和表锁X/S有相应的兼容性判断：

| -    | IS               | IX     | S      | X                |
| ---- | ---------------- | ------ | ------ | ---------------- |
| IS   | 兼容(compatible) | 兼容   | 兼容   | 不兼容(conflict) |
| IX   | 兼容             | 兼容   | 不兼容 | 不兼容           |
| S    | 兼容             | 不兼容 | 兼容   | 不兼容           |
| X    | 不兼容           | 不兼容 | 不兼容 | 不兼容           |

————————————————————————————————————

除了通过锁来进行并发控制（**一致性锁定读**，select for update/select for shared/update where / delete where）；另外，在默认情况下。事务第一次读的时候会通过undo空间提供的多版本，构建一个readview，提供**一致性非锁定读**；这就是RR级别下，可重复读的实现方式。比如，`mysqldump --single-transaction`时，就是基于RR级别的读快照进行导出。

————————————————————————————————————

另外，还有一种特殊的表锁：Auto-Inc Lock，当有AUTO_INCREMENT列时，插入数据时会有这个锁，由参数**innodb_autoinc_lock_mode**控制自增长的控制算法。由于并发插入的存在，自增长的值是不连续的；那么，基于statement的主从复制可能出现问题；因此，启用auto_increment后，需要是有row模式的主从复制。

### 行锁

![image-20190712104111800](/image/InnoDB-lock.png)

+ **Record Lock**：基于主键锁定某个记录

+ **Gap Lock**：要求隔离级别是RR，并且innodb_locks_unsafe_for_binlog=0；这时，如果查询走非唯一索引或者查询是范围读，那么会加GapLock。

+ **Next-Key Lock**：前提是启用了GapLock，其是Record Lock和该Record之前区间的Gap Lock的结合；否则，只是recordLock。

  当给一个record加x/s锁时，其实是给该record加recordlock，和该record之前的一个gap加了gaplock；即给一个左开右闭的区间加了锁。避免幻读。

  当查询的索引具有唯一性时，Next-Key Lock降级为Record Lock。

+ **Insert Intention Lock**：Insert语句的特殊的GapLock；插入数据时，需要请求插入间隙的GapLock；避免并发对同一个间隙的插入。

```c
/* Precise modes */
#define LOCK_ORDINARY	0	/*!< this flag denotes an ordinary
				next-key lock in contrast to LOCK_GAP
				or LOCK_REC_NOT_GAP */
#define LOCK_GAP	512	/*!< when this bit is set, it means that the
				lock holds only on the gap before the record;
				for instance, an x-lock on the gap does not
				give permission to modify the record on which
				the bit is set; locks of this type are created
				when records are removed from the index chain
				of records */
#define LOCK_REC_NOT_GAP 1024	/*!< this bit means that the lock is only on
				the index record and does NOT block inserts
				to the gap before the index record; this is
				used in the case when we retrieve a record
				with a unique key, and is also used in
				locking plain SELECTs (not part of UPDATE
				or DELETE) when the user has set the READ
				COMMITTED isolation level */
#define LOCK_INSERT_INTENTION 2048 /*!< this bit is set when we place a waiting
				gap type record lock request in order to let
				an insert of an index record to wait until
				there are no conflicting locks by other
				transactions on the gap; note that this flag
				remains set when the waiting lock is granted,
				or if the lock is inherited to a neighboring
				record */
#define LOCK_PREDICATE	8192	/*!< Predicate lock */
#define LOCK_PRDT_PAGE	16384	/*!< Page lock */
```

> `innodb_locks_unsafe_for_binlog`
>
> 该参数的作用和将隔离级别设置为 READ COMMITTED相同，是一个将要废弃的参数。

### MySQL层的加锁相关操作

`handle_query`，MySQL执行每个SQL语句分为五步：

```
 - Preparation
 - Locking of tables
 - Optimization
 - Execution or explain
 - Cleanup
```

在第二步，调用`ha_innobase::external_lock`对表加表级别的锁；非显式LOCK时，在执行每条语句之前执行该函数，进行表级锁定；后续根据SQL类型调用不同的函数处理。

> 在执行一次查询中，以为会走到row_sel，但是没有走到；而是走到了row_search_mvcc。那么row_search_mvcc和row_sel的区别是什么？
>
> - row_search_mvcc：在InnoDB的向上接口中（index_read）调用，读取InnoDB中的数据；并且利用mvcc的机制，读取该事务应该看到的数据。
> - row_sel_step：Query graph中的select步骤，[基本没啥用](https://www.slideshare.net/plinux/mysql01)。

**Select是如何执行的（ha_innobase::index_read）？**

`execute_sqlcom_select`

如果用户这里只讨论用户非显式指定表锁的查询过程。首先对于每个表，可能存在多个table handle实例。

1. 调用external_lock加锁（底层是调用InnoDB的external_lock）；并设置其他参数，比如m_prebuilt->sql_stat_start和trx->n_mysql_tables_in_use。
2. 如果m_prebuilt->sql_stat_start为true，在index_read中，将预先设置好m_prebuilt->template（其中应该是表数据结构的设置）。
3. 调用row_search_for_mysql，如果m_prebuilt->sql_stat_start为true，那么创建一个该事务的readview。
4. 执行SELECT，执行index_read；如果是join，可能多次执行。
5. SELECT结束，释放external_lock中的锁；如果n_mysql_tables_in_use为0：
   1. 当autocommit=0，执行commit
   2. 释放sql语句占用的资源。

在查询的时候，在mysql层将select/insert/update进行分发；对于select只有读操作，会在执行之前在表上加is锁；然后调用row_search_mvcc，这里分为6步（代码注释）：

1. 释放AHI上的s-latch

2. `row_sel_dequeue_cached_row_for_mysql`，从预读的cache中读取行

3. 查找AHI；对于可能会外部存储的行（比如text类型），不能使用AHI

   > we cannot use the adaptive hash index in a search in the case the row may be long and there may be externally stored fields
   >
   > !prebuilt->templ_contains_blob

4. 打开或还原一个index cursor；根据是不是第一次执行（prebuilt->sql_stat_start），决定要不要创建一个readview（trx_assign_read_view(trx);），或者加意向锁（err = lock_table(0, index->table, prebuilt->select_lock_type == LOCK_S  ? LOCK_IS : LOCK_IX, thr);）。

   然后，开始查询`btr_pcur_open_with_no_init->btr_cur_search_to_nth_level`，定位索引位置。

5. 在索引位置处，找到匹配的元组。

6. cursor移动到下一个元组

这里的读操作是基于一个cursor，开始的时候会根据一致性锁定读还是非锁定读，决定创建一个readview还是加意向锁；row_search_mvcc通过上层的get_next调用；每次row_search_mvcc读取一行，然后将cursor保存起来，下次再restore读取。

### 监控视图

```sql
select * from information_schema.innodb_trx\G; -- 查看当前的事务信息
select * from information_schema.innodb_locks\G; --查看当前的锁信息
select * from information_schema.innodb_lock_waits\G; --- 查看当前的锁等待信息
--可以联表查，查找自己想要的结果。
select * from sys.innodb_lock_waits\G; -- 查看当前的锁等待信息
show engine innodb status\G;
---还可以通过当前执行了执行了什么语句
select * from  performance_schema.events_statements_current\G; 
show full processlist;
```

> **注意**没有幻读有幻写
>
> + 其他事务更新了数据
>
> ```sql
> mysql> start transaction;
> mysql> select * from t;
> +-----+--------+------+---------+------+
> | a   | b      | c    | d       | e    |
> +-----+--------+------+---------+------+
> ...
> | 394 | asdf | asdf | asdf    |  399 |
> | 395 | asdf | asdf | asdf    |  400 |
> | 397 | asdf | asdf | asdfasd |  402 |
> +-----+------+------+---------+------+
> Query OK, 0 rows affected (0.00 sec)
> mysql> select * from t where a = 396;
> Empty set (0.00 sec)
> 
> mysql> update t set b = 'pwrite' where a = 396;
> Query OK, 1 row affected (0.00 sec)
> Rows matched: 1  Changed: 1  Warnings: 0
> 
> mysql> select * from t where a = 396;
> +-----+--------+------+---------+------+
> | a   | b      | c    | d       | e    |
> +-----+--------+------+---------+------+
> | 396 | pwrite | asdf | asdfasd |  402 |
> +-----+--------+------+---------+------+
> 1 row in set (0.00 sec)
> 
> mysql> commit;
> Query OK, 0 rows affected (0.01 sec)
> ```
>
> 在第3行查询之前，在另一个事务中执行如下更新：
>
> ```sql
> update t set a = 396 where e = 402;
> ```
>
> + 其他事务插入了数据
>
> ```sql
> | 395 | asdf       | asdf | asdf    |  400 |
> | 396 | pwrite     | asdf | asdfasd |  402 |
> | 397 | new insert | asdf | s       |  403 |
> | 398 | new insert | s    | s       |  404 |
> +-----+------------+------+---------+------+
> 399 rows in set (0.01 sec)
> 
> mysql> update t set e=405 where a = 399;
> Query OK, 0 rows affected (0.00 sec)
> Rows matched: 1  Changed: 0  Warnings: 0
> 
> mysql> commit;
> Query OK, 0 rows affected (0.00 sec)
> ```
>
> 发现：
>
> 当前事务select不可见，即，不能看到新事务提交的数据，满足可重复读；
>
> 但是当前事务执行update，却能够更新；更新之后再select，可以看到这个新元组？
>
> 因此，可知MySQL的RR级别的实现，在read的时候确实更加严格没有幻读了。但是，事务需要修改的时候，对于其他事务新插入的数据，是不能看到的；对于其他事务修改的数据是可以看到了的，😹还有这种操作。。。
>
> 因此，对于MySQL的RR级别，有如下结论：
>
> 1. 当只是select语句时，是没有幻读（Phantom Read）的；比如*mysqldump with –single-transaction*。
> 2. 当事务修改数据了，RR级别的表现是有所不同；对于没有修改的行，是RR；对于修改的行，是RC。因为，SQL标准中对此没有定义，那么也不能说违反了SQL语义。
> 3. 当事务写了新数据时，该事务就使用已经提交的数据，而不是该事务的readview；所以，InnoDB的事务修改总是基于最新的提交的数据进行修改。

## 隐式内存锁

基于系统提供的原子操作，实现的内存并发访问机制：

+ **mutex**，内存结构的串行访问，主要用在一些共享的数据结构上。
  + Dictionary mutex（Dictionary header)
  + Transaction undo mutex，Transaction system header的并发访问，在修改indexpage前，在Transaction system的header中写入一个undo log entry。
  + Rollback segment mutex，Rollback segment header的并发访问，当需要在回滚段中添加一个新的undopage时，需要申请这个mutex。
  + lock_sys_wait_mutex：lock timeout data
  + lock_sys_mutex：lock_sys_t
  + trx_sys_mutex：trx_sys_t
  + Thread mutex：后台线程调度的mutex
  + query_thr_mutex：保护查询线程的更改
  + trx_mutex：trx_t
  + Search system mutex
  + Buffer pool mutex
  + Log mutex
  + Memory pool mutex 

+ **rw_lock（latch）**，读写操作的并发访问，在MySQL中主要就是针对Btree的并发访问，其中有两种锁粒度：index和block。而对于树结构的访问，如果只是读操作，那么，non-leaf节点只是用来查找leafnode，当找到之后，分支的lock可以释放了；而如果是写操作，只有需要节点分裂或者合并，那么整条路径都需要加xlock（当insert时，判断leafnode是否非满；当delete时，判断leafnode中记录数是否大于一半）。
  + Secondary index tree latch ，Secondary index non-leaf 和 leaf的读写
  + Clustered index tree latch，Clustered index non-leaf 和 leaf的读写
  + Purge system latch，Undo log pages的读写，
  + Filespace management latch，file page的读写
  + 等等

### InnoDB的rw_lock

rw_lock的相关操作在`sync/sync0rw.cc`中，共有四种类型，（在5.7新加了一个[SX](https://dev.mysql.com/worklog/task/?id=6363)类型），用在btree和mtr中。

```c
enum rw_lock_type_t {
	RW_S_LATCH = 1,
	RW_X_LATCH = 2,
	RW_SX_LATCH = 4,
	RW_NO_LATCH = 8
};
/*
 LOCK COMPATIBILITY MATRIX
    S SX  X
 S  +  +  -
 SX +  -  -
 X  -  -  -
 */
```

通过新加的和这个SX rwlock，进一步缩小的X的范围；即，当修改数据页时会导致树结构发生变化时，只将可能发生变化的branch page加X，而在整个树上加SX即可；这样其他分支的读操作就不会受到影响。

在对Btree操作时，针对如下Btree的不同操作，对Btree的Index(内存dict_cache中的dict_index_t结构)或者block加不同模式的rw_lock。

```c

/** Latching modes for btr_cur_search_to_nth_level(). */
enum btr_latch_mode {
	/** Search a record on a leaf page and S-latch it. */
	BTR_SEARCH_LEAF = RW_S_LATCH,
	/** (Prepare to) modify a record on a leaf page and X-latch it. */
	BTR_MODIFY_LEAF	= RW_X_LATCH,
	/** Obtain no latches. */
	BTR_NO_LATCHES = RW_NO_LATCH,
	/** Start modifying the entire B-tree. */
	BTR_MODIFY_TREE = 33,
	/** Continue modifying the entire B-tree. */
	BTR_CONT_MODIFY_TREE = 34,
	/** Search the previous record. */
	BTR_SEARCH_PREV = 35,
	/** Modify the previous record. */
	BTR_MODIFY_PREV = 36,
	/** Start searching the entire B-tree. */
	BTR_SEARCH_TREE = 37,
	/** Continue searching the entire B-tree. */
	BTR_CONT_SEARCH_TREE = 38
};
```

#### 加锁入口

入口是`btr_cur_search_to_nth_level`，因为不管查询还是修改都是先定位具体的page，进行相应的加锁操作；然后再进行读取或者变更。

该函数参数`latch_mode`，低位是`btr_latch_mode`枚举，高位是若干不同意义的宏（include/btr0btr.h），宏根据insert/delete/delete_mark分为互斥的三类；通过latch_mode判断加锁的**粒度**和**力度**。

如下是该函数的加锁逻辑：

1. 函数一开始，识别高位的标记得到如下信息后，将高位信息抹除。
   + btr_op：决定ibuf的操作（btr0cur.c:1117）
   
     ```c
     /** Buffered B-tree operation types, introduced as part of delete buffering. */
  enum btr_op_t {
     	BTR_NO_OP = 0,			/*!< Not buffered */
     	BTR_INSERT_OP,			/*!< Insert, do not ignore UNIQUE */
     	BTR_INSERT_IGNORE_UNIQUE_OP,	/*!< Insert, ignoring UNIQUE */
     	BTR_DELETE_OP,			/*!< Purge a delete-marked record */
     	BTR_DELMARK_OP			/*!< Mark a record for deletion */
     };
     ```
   
   + estimate：在查询优化阶段，调用的`btr_cur_search_to_nth_level`
   
   + lock_intention：要对Btree进行的修改意图。
   
     ```c
     /** Modification types for the B-tree operation. */
     enum btr_intention_t {
     	BTR_INTENTION_DELETE,
     	BTR_INTENTION_BOTH,
     	BTR_INTENTION_INSERT
     };
     ```
   
   + modify_external：在BTR_MODIFY_LEAF模式中，是否要修改外部存储的数据。
   
2. `btr_search_guess_on_hash`，首先尝试基于AHI查询，成功就返回。

3. 在第一步中，将高位的标记信息已经抹除；这里（btr0cur.cc:959）基于latch_mode和第一步解析处理的信息，决定`upper_rw_latch`的力度；

   这里测试时是简单查询，因此给index加s。

   `mtr_s_lock(dict_index_get_lock(index), mtr);`

   除了upper_rw_latch外，还有`root_leaf_rw_latch`

4. 根据参数`mode`定义的查询模式 ，决定非叶子节点的查询模式（1043）。

   ```c
   /* Page cursor search modes; the values must be in this order! */
   enum page_cur_mode_t {
   	PAGE_CUR_UNSUPP	= 0,
   	PAGE_CUR_G	= 1,
   	PAGE_CUR_GE	= 2,
   	PAGE_CUR_L	= 3,
   	PAGE_CUR_LE	= 4,
   
   /*      PAGE_CUR_LE_OR_EXTENDS = 5,*/ /* This is a search mode used in
   				 "column LIKE 'abc%' ORDER BY column DESC";
   				 we have to find strings which are <= 'abc' or
   				 which extend it */
   
   /* These search mode is for search R-tree index. */
   	PAGE_CUR_CONTAIN		= 7,
   	PAGE_CUR_INTERSECT		= 8,
   	PAGE_CUR_WITHIN			= 9,
   	PAGE_CUR_DISJOINT		= 10,
   	PAGE_CUR_MBR_EQUAL		= 11,
   	PAGE_CUR_RTREE_INSERT		= 12,
   	PAGE_CUR_RTREE_LOCATE		= 13,
   	PAGE_CUR_RTREE_GET_FATHER	= 14
   };
   ```

5. (search_loop)递归查找，直到到达指定层

   1. 确定rw_latch的模型

   2.  插入一级索引（可使用AHI）

   3. 插入二级索引（可能使用ibuf）

      1. buf_page_get_gen读取具体的page；

         如果block=null，那么就要用ibuf

      2. 判断是否锁定left sibling

      3. 取出page（`page = buf_block_get_frame(block);`）

6. (1862)找到后设置cursor的low_match和up_match等参数
7. 函数退出，因为调用`btr_cur_search_to_nth_level`的调用者可能已经在外面加锁了，由参数has_search_latch判断，该参数只能为0或者`RW_S_LATCH`；如果设置了该参数，那么退出是会对index加s锁 `rw_lock_s_lock(btr_get_search_latch(index))`。

————————————————————————————————————

#### SELECT的rwlock



#### INSERT的rwlock

#### 总结

查询扫描前，在索引树上加`btr_search_s_lock`；找到之后释放

升序或降序扫描时，都是先获取下一个page的锁，然后再释放现在的锁；为了避免死锁，都是先获取左page的lock，之后再持有下一个page的锁

插入时，需要RW_X_LATCH对应的leafpage。

- 如果需要叶子分裂，首先在整个index上加X。然后再三个leafpage（prev、current、next）上加X。正在读取的leafpage不受影响，但是后续会阻塞。

# 参考

[mysql-rr](https://blog.pythian.com/understanding-mysql-isolation-levels-repeatable-read/)

[mysql-index-lock](http://mysql.taobao.org/monthly/2015/07/05/)