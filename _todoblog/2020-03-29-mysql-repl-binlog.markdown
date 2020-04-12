---
layout: post
title: MySQL并行复制（MTS）解析
date: 2020-03-29 13:40
categories:
  - MySQL
typora-root-url: ../../layamon.github.io
---
> * TOC
{:toc}

在[另一个blog]()中，阐述了binlog以及group commit的机制，binlog在MySQL中主要是用来做主从同步的，为了提高从库的性能，在5.6中引入了基于database的并行复制，之后又有了基于Logical_clock（group commit）的并行复制。本文基于笔者对MySQL5.7代码的阅读，阐述MySQL基于binlog的主从同步的内部机理。

## 配置并行从库

1. mysql_dump

2. Reset master

   (如果使用了基于gtid的复制)，否则可能有错误：“ERROR 1840 (HY000) at line 33: @@GLOBAL.GTID_PURGED can only be set when @@GLOBAL.GTID_EXECUTED is empty”。

3. restore

4. change master

5. start slave

关于配置个人操作不是很熟练，这里有个脚本[https://github.com/Layamon/mysql-bash/blob/master/rpl_conf.sh]

## GTID-based replication

MySQL的GTID是全局事务ID，每个事务由GTID标识，在master上跟踪事务状态并在slave上应用。gtid在整个复制集群中是唯一的，slave中如果重做了某个gtid标识的事务，那么后续该gtid标识的事务则不予理睬。

在master上，客户端事务提交的时候会写入到binlog中(`MYSQL_BIN_LOG::write_gtid`)，然后分配一个gtid；并且gtid的生成确保是单调的，并且是没有空隙的。如果事务没有写入到binlog中，那么就不会生成gtid（比如事务是只读的）。

系统表`mysql.gtid_executed`维护了已经应用过的gtid。如下：

```sql
root@127.0.0.1 [sbtest]
> show master status;
+---------------+----------+--------------+------------------+------------------------------------------+
| File          | Position | Binlog_Do_DB | Binlog_Ignore_DB | Executed_Gtid_Set                        |
+---------------+----------+--------------+------------------+------------------------------------------+
| binlog.000005 |      194 |              |                  | 3acb9a39-74b8-11ea-8821-fa163e02960f:1-3 |
+---------------+----------+--------------+------------------+------------------------------------------+
1 row in set (0.00 sec)

root@127.0.0.1 [sbtest]
> select * from mysql.gtid_executed;
+--------------------------------------+----------------+--------------+
| source_uuid                          | interval_start | interval_end |
+--------------------------------------+----------------+--------------+
| 3acb9a39-74b8-11ea-8821-fa163e02960f |              1 |            2 |
| 3acb9a39-74b8-11ea-8821-fa163e02960f |              3 |            3 |
+--------------------------------------+----------------+--------------+
2 rows in set (0.00 sec)
```

在slave端，如果某个gtid对应的binlog正在执行，那么此时如果执行相同gtid的binlog会被block。binlog中也有gtid的状态相关的event（Gtid_log_event），如果崩溃的时候没有及时更新gtid_executed的数据，那么恢复的时候会从binlog中读取。

GTID具体就是由冒号连接的两个ID的组合：

```ini
GTID = source_id:transaction_id
```

source_id就是master的server_id（server_uuid上面配置中体现）。transaction_id是一个序列值，表示哪些事务在master上commit成功了。比如，`3E11FA47-71CA-11E1-9E33-C80AA9429562:23`表示在uuid=3E11FA47-71CA-11E1-9E33-C80AA9429562的server上事务号23的事务提交成功了。

> **GTID set**
>
> ```
> 2174B383-5441-11E8-B90A-C80AA9429562:1-3:11:47-49, 24DA167-0C0C-11E8-8442-00059A3C7B00:1-19
> ```
>
> 一组gtid可以表示为一个gtid set；如上例，具体语法如下。
>
> ```
> gtid_set:
>  uuid_set [, uuid_set] ...
>  | ''
> 
> uuid_set:
>  uuid:interval[:interval]...
> 
> uuid:
>  hhhhhhhh-hhhh-hhhh-hhhh-hhhhhhhhhhhh
> 
> h:
>  [0-9|A-F]
> 
> interval:
>  n[-n]
> 
>  (n >= 1)
> ```

### GTID的生命周期

1. 当事务提交需要写binlog时，分配一个gtid；并将这个gtid事件写盘（Gtid_log_event），并且该gtid在事务日志之前。

2. 当binlog要rotate或者server关闭时，server将之前的binlog的所有事务的gtid写入到gtid_executed中。

3. 事务提交后，将gtid非原子性地更新在全局变量`@@GLOBAL.gtid_executed`中；在复制机制中，该变量表示了服务的当前状态。

4. slave收到binlog并落盘为relaylog后，读取gtid然后设置全局变量`gtdi_next`；该变量表示下一个事务必须使用这个gtid（slave可在session级别设置该变量）。

5. slave确认该gtid当前没人正在使用；

   > 这里可以了解一下gtid_owned变量，这是全局只读的参数；如果要确保只有一个线程在执行该gtid事务相关的日志，可以在session级别设置，标明了每个gtid和threadid的对应关系。

6. 某线程获得gtid_next的gtid的所有权后，slave开始恢复对应事务；这里slave不会产生新的事务。

7. 如果slave开启了binlog，事务提交时，会在之前写入Gtid_log_event的日志；如果binlog轮转，那么就在系统表gtid_executed中写入之前日志所有的gtid。

8. 如果slave没有开启binlog，gtid会原子性地写入到gtid_executed表中；然后再事务日志中加一条插入系统表的操作。这时gtid_executed表在主从上都是完整的记录。（这里在5.7中，DML事务是原子的，DDL不是；那么在DDL事务中，可能GTID不一致）。

## 并行复制

参数

+ slave_parallel_type
+ slave_parallel_workers           // worker 线程个数
+ slave-checkpoint-group           // 隔多少个事务做一次 checkpoint
+ slave-checkpoint-period          // 隔多长时间做一次 checkpoint
+ slave-pending-jobs-size-max      // 分发给worker的、处于等待状态的event的大小上限

由于在MySQL中写入是基于锁的并发控制，所以所有在Master端同时处于prepare阶段且未提交的事务就不会存在锁冲突，在Slave端执行时都可以并行执行。因此可以在所有的事务进入prepare阶段的时候标记上一个logical timestamp（实现中使用上一个提交事务的sequence_number，即last_commit_id），在Slave端同样timestamp的事务就可以并发执行。

在MySQL5.6后出现schema级别的并行；在5.7后，基于组提交的原理，确保了同一个commit group内的事务没有冲突，这样同一个group内的trx就可以并行了。

```
This optimization reduces the number of operations needed to produce the binary logs by grouping transactions. When transactions are committing at the same time, they are written to the binary log in a single operation. But if transactions commit at the same time, then they are not sharing any locks, which means they are not conflicting thus can be executed in parallel on slaves. 
```

因此，将group commit的信息添加到binlog中，slave可以安全的并行replay。基于Lock-based的并发复制，在每次事务DML时，都会更新一次last_commited，见`binlog_prepare`

```cpp
class Transaction_ctx{
  /* Binlog-specific logical timestamps. */
  /*
    Store for the transaction's commit parent sequence_number.
    The value specifies this transaction dependency with a "parent"
    transaction.
    The member is assigned, when the transaction is about to commit
    in binlog to a value of the last committed transaction's sequence_number.
    This and last_committed as numbers are kept ever incremented
    regardless of binary logs being rotated or when transaction
    is logged in multiple pieces.
    However the logger to the binary log may convert them
    according to its specification.
  */
  int64 last_committed;
  int64 sequence_number;
}
```

引擎层提交前，更新最大的commit_id;(MYSQL_BIN_LOG::finish_commit)

```cpp
  if (!cache_mngr->stmt_cache.is_binlog_empty())
  {
    /*
      Commit parent identification of non-transactional query has
      been deferred until now, except for the mixed transaction case.
    */
    trn_ctx->store_commit_parent(m_dependency_tracker.get_max_committed_timestamp());
    if (cache_mngr->stmt_cache.finalize(thd))
      DBUG_RETURN(RESULT_ABORTED);
    stmt_stuff_logged= true;
  }
```

并发复制时，last_master_timestamp的更新是在从库事务执行结束后，如果出现一个很大的事务，主从延迟的时间就会变大



wait_for_last_committed_trx(rli, last_committed, lwm_estimate)



ref：

http://mysql.taobao.org/monthly/2016/03/09/

http://mysql.taobao.org/monthly/2017/12/03/

http://frodo.looijaard.name/article/mysql-backups-mysqldump

https://mysqlhighavailability.com/multi-threaded-replication-performance-in-mysql-5-7/

https://cloud.tencent.com/developer/article/1429685

http://yoshinorimatsunobu.blogspot.com/2015/01/performance-issues-and-fixes-mysql-56.html

https://bugs.mysql.com/bug.php?id=73066

http://mysql.taobao.org/monthly/2018/05/09/

http://mysql.taobao.org/monthly/2015/08/09/

http://mysql.taobao.org/monthly/2018/06/02/