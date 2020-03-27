---
layout: post
title: MySQL-Binlog由很浅到浅
date: 2019-06-11 18:28
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - MySQL
typora-root-url: ../../layamon.github.io
---

* TOC
{:toc}
作为一个MySQL初学者，BinLog是MySQL经常提到的很关键的一个模块，这篇文章整理了对这方面的总结，从基本配置开始。

# binlog配置与管理

+ MySQL的配置文件

```bash
>  mysqld --datadir=/Users/liuyangming/dbdata/mysql/data --help --verbose | grep -A 1 'Default options'
Default options are read from the following files in the given order:
/etc/my.cnf /etc/mysql/my.cnf /usr/local/mysql/etc/my.cnf ~/.my.cnf
```

+ 开启binlog的配置

```ini
[mysqld]
performance_schema=ON
log-bin=bin.log
log-bin-index=bin-log.index
max_binlog_size=100M
binlog_format=row
server-id=1
```

+ 重启MySQL

```
mysqladmin -u root -p shutdown

/usr/local/mysql/bin/mysqld_safe --datadir=/Users/liuyangming/dbdata/mysql/data &
```

+ 查看binlog

```sql
mysql> show global variables like "log_bin";
+---------------+-------+
| Variable_name | Value |
+---------------+-------+
| log_bin       | ON    |
+---------------+-------+
1 row in set (0.01 sec)

mysql> show global variables like "binlog_format";
+---------------+-------+
| Variable_name | Value |
+---------------+-------+
| binlog_format | ROW   |
+---------------+-------+
1 row in set (0.01 sec)

mysql> show binary logs;
+------------+-----------+
| Log_name   | File_size |
+------------+-----------+
| bin.000001 |       177 |
| bin.000002 |      1372 |
| bin.000003 |       195 |
| bin.000004 |       154 |
+------------+-----------+
4 rows in set (0.01 sec)
```

在如下三种情况下，binlog会发生切换：

1. MySQL重启

2. 当前binlog段大小超过`max_binlog_size`（注意如果有大事务没结束，不会切换）。

3. flush logs

   ```sql
   mysql> show binary logs;
   +------------+-----------+
   | Log_name   | File_size |
   +------------+-----------+
   | bin.000001 |       177 |
   | bin.000002 |      1372 |
   | bin.000003 |       195 |
   | bin.000004 |       154 |
   +------------+-----------+
   4 rows in set (0.01 sec)
   
   mysql> flush logs;
   Query OK, 0 rows affected (0.09 sec)
   
   mysql> show binary logs;
   +------------+-----------+
   | Log_name   | File_size |
   +------------+-----------+
   | bin.000001 |       177 |
   | bin.000002 |      1372 |
   | bin.000003 |       195 |
   | bin.000004 |       195 |
   | bin.000005 |       154 |
   +------------+-----------+
   5 rows in set (0.00 sec)
   ```

## binlog内容

binlog是一组binlog文件加一个索引文件，包含了MySQL中的数据更改。每个log文件由一个magic数(`#define BINLOG_MAGIC        "\xfe\x62\x69\x6e"`，即"\xfe bin")和一组event构成。

```sql
mysql> show binlog events in 'bin.000001';
```

<img src="/image/mysql-binlog/binlog-example.png" alt="image-20190612084052992" style="zoom: 67%;" />

每个log的第一个event都是一个`Format_description_log_event`；描述了当前的log文件是什么格式。后续的event就按照这个格式进行解析；最后一个event是log-rotation事件，描述了下一个binlog文件的名称，如下：

```c
mysql> show binlog events in 'bin.000003';
+------------+-----+----------------+-----------+-------------+---------------------------------------------+
| Log_name   | Pos | Event_type     | Server_id | End_log_pos | Info                                        |
+------------+-----+----------------+-----------+-------------+---------------------------------------------+
| bin.000003 |   4 | Format_desc    |         1 |         123 | Server ver: 5.7.26-debug-log, Binlog ver: 4 |
| bin.000003 | 123 | Previous_gtids |         1 |         154 |                                             |
| bin.000003 | 154 | Rotate         |         1 |         195 | bin.000004;pos=4                            |
+------------+-----+----------------+-----------+-------------+---------------------------------------------+
3 rows in set (0.01 sec)
```

binlog的索引文件就是一个文本文件，记录了当前的binlog的文件，如下：

```bash
> cat bin-log.index
./bin.000001
./bin.000002
./bin.000003
./bin.000004
./bin.000005
```

## Slave Relay Log

binlog主要用在MySQL的主从集群中，在master端有Binlog dumthread负责发送binlog；在slave段有两个线程：IO/SQL负责这一过程。

IO thread：从master端读取binlog，然后写入到本地的relaylog中。

SQL thread：读取relay log，按照event重做数据。

通过`show slave status`可以查看复制的进度。当SQL线程执行完某个relaylog上的所有event，那么就将该文件删除。在如下三个情况下，MySQL slave会创建一个新的relaylog：

+ IO线程启动
+ FLUSH LOGS
+ 当前relaylog大于配置项([`max_relay_log_size`](https://dev.mysql.com/doc/refman/5.7/en/replication-options-slave.html#sysvar_max_relay_log_size))。

## GTID-based replication

全局事务ID，每个事务由GTID标识，在master上跟踪事务状态并在slave上应用。gtid在整个复制集群中是唯一的，slave中如果重做了某个gtid标识的事务，那么后续该gtid标识的事务则不予理睬。

在master上，客户端事务提交的时候会写入到binlog中。然后分配一个gtid；并且gtid的生成确保是单调的，并且是没有空隙的。如果事务没有写入到binlog中，那么就不会生成gtid（比如事务是只读的）。

在slave端，系统表`mysql.gtid_executed`维护了已经应用过的gtid。如果某个gtid对应的binlog正在执行，那么此时如果执行相同gtid的binlog会被block。

GTID具体就是由冒号连接的两个ID的组合：

```ini
GTID = source_id:transaction_id
```

source_id就是master的server_id（server_uuid上面配置中体现）。transaction_id是一个序列值，表示那些事务在master上commit成功了。比如，`3E11FA47-71CA-11E1-9E33-C80AA9429562:23`表示在uuid=3E11FA47-71CA-11E1-9E33-C80AA9429562的server上事务号23的事务提交成功了。

> GTID set
>
> ```
> 2174B383-5441-11E8-B90A-C80AA9429562:1-3:11:47-49, 24DA167-0C0C-11E8-8442-00059A3C7B00:1-19
> ```
>
> 一组gtid可以表示为一个gtid set；如上例，具体语法如下。
>
> ```
> gtid_set:
>     uuid_set [, uuid_set] ...
>     | ''
> 
> uuid_set:
>     uuid:interval[:interval]...
> 
> uuid:
>     hhhhhhhh-hhhh-hhhh-hhhh-hhhhhhhhhhhh
> 
> h:
>     [0-9|A-F]
> 
> interval:
>     n[-n]
> 
>     (n >= 1)
> ```

在系统表mysql.gtid_executed中，维护了gtid的状态；binlog中也有gtid的状态，如果崩溃的时候没有及时更新gtid_executed的数据，那么恢复的时候会从binlog中读取。

### GTID的生命周期

1. 当事务提交需要写binlog时，分配一个gtid；并将这个gtid事件写盘（Gtid_log_event），并且该gtid在事务日志之前。
2. 当binlog要轮转或者server关闭时，server将之前的binlog的所有事务的gtid写入到gtid_executed中。
3. 事务提交后，将gtid非原子性地更新在全局变量`@@GLOBAL.gtid_executed`中；在复制机制中，该变量表示了服务的当前状态。
4. slave收到binlog并落盘为relaylog后，读取gtid然后设置全局变量`gtdi_next`；该变量表示下一个事务必须使用这个gtid（slave在session级别设置该变量）。
5. slave确认该gtid当前没人正在使用，如果有确保只有一个线程在执行该gtid事务相关的日志；在系统变量：gtid_owned中标明了每个gtid和threadid的对应关系。
6. 某线程获得gtid_next的gtid的所有权后，slave开始恢复对应事务；这里slave不会产生新的事务。
7. 如果slave开启了binlog，事务提交时，会在之前写入Gtid_log_event的日志；如果binlog轮转，那么就在系统表gtid_executed中写入之前日志所有的gtid。
8. 如果slave没有开启binlog，gtid会原子性地写入到gtid_executed表中；然后再事务日志中加一条插入系统表的操作。这时gtid_executed表在主从上都是完整的记录。（这里在5.7中，DML事务是原子的，DDL不是；那么在DDL事务中，可能GTID不一致）。
9. TODO

# BinLog代码模块

binlog中按照事务的先后顺序组织，每个事务分为若干个event。binlog按照event写入相关的数据，slave按照event调用对应的函数回放数据。

client/mysqlbinlog.cc：解析binlog的工具。

sql/log.cc：该模块提供了宏观上操作的工具函数，比如创建/删除/写入binlog文件等。

[sql/log_event.cc](https://dev.mysql.com/doc/internals/en/event-classes-and-types.html)：底层操作log的函数，其中有各种event与相应的操作类的对应关系。

sql/rpl_contants.h：INCIDENT_EVENT事件的代码

sql/slave.cc：slave端的IO和SQL线程处理binlog的代码

sql/rpl_injector.cc：injector允许在binlog中进行额外的插入，用在组复制场景。

sql/rpl_tblmap.cc：数字到具体表的映射，用在row模式的log中，用来确定表。

sql/rpl_utility.cc：辅助类用在Table_map_events的处理；另外，这里还有一些智能指针的实现。

sql/sql_binlog.cc：执行binlog中的语句；为了确认[BINGLOG的模式](https://dev.mysql.com/doc/internals/en/binary-log-versions.html)，第一个event必须是Format_description_log_event。

sql/sql_base.cc：其他模块公用的基础函数。

## Event的细节

> [各个event的含义](https://dev.mysql.com/doc/internals/en/event-meanings.html)

每个event都代表一个数据库的操作，每个event由一个header加具体数据构成；header的结构如下，包括何时产生，由哪个server产生等信息：

```c
    +---------+---------+---------+------------+-----------+-------+
    |timestamp|type code|server_id|event_length|end_log_pos|flags  |
    |4 bytes  |1 byte   |4 bytes  |4 bytes     |4 bytes    |2 bytes|
    +---------+---------+---------+------------+-----------+-------+
```

event的内容按照如下约定写入：

+ 数字按照little-endian存储。

  ![Image result for little-endian](/image/mysql-binlog/bigLittleEndian.png)

+ 代表位置和长度的值，按照byte表示。

+ 有些数字是用Packed Integer表示

  > Packed Integer
  >
  > 整型数字可以占用8字节，也可以占用1、2、4字节；有第一个字节的值来标明：
  >
  > | **First byte** | **Format**                                                   |
  > | -------------- | ------------------------------------------------------------ |
  > | 0-250          | The first byte is the number (in the range 0-250). No additional bytes are used. |
  > | 252            | Two more bytes are used. The number is in the range 251-0xffff. |
  > | 253            | 还有三个字节，值从0xffff-0xffffff.                           |
  > | 254            | 还有8个字节 0xffffff-0xffffffffffffffff.                     |
  >
  > 第一个byte的251没有用到，用这个值表示SQL中的NULL。

+ 字符串有很多写入格式：

  + 写入到一个固定大小的空间，后面用0x00补齐
  + 变长串前加一个length字段
  + 变长串可能以null结尾，也可能不是；（相应的描述符中有标明）
  + null结尾的变长串的前置length不包含null。
  + 如果变长串是event内容的最后一个，如果没有length前缀；那么就可以通过event的长度得出。

## 事务binlog event写入流程

binlog cache和binlog临时文件都是在事务运行过程中写入，一旦事务提交，binlog cache和binlog临时文件都会释放掉。而且如果事务中包含多个DML语句，他们共享binlog cache和binlog 临时文件。整个binlog写入流程：

1. 事务开启
2. 执行dml语句，在dml语句第一次执行的时候会分配内存空间binlog cache
3. 执行dml语句期间生成的event不断写入到binlog cache
4. 如果binlog cache的空间已经满了，则将binlog cache的数据写入到binlog临时文件，同时清空binlog cache。如果binlog临时文件的大小大于了max_binlog_cache_size的设置则抛错ERROR 1197
5. 事务提交，整个binlog cache和binlog临时文件数据全部写入到binlog file中，同时释放binlog cache和binlog临时文件。但是注意此时binlog cache的内存空间会被保留以供THD上的下一个事务使用，但是binlog临时文件被截断为0，保留文件描述符。其实也就是IO_CACHE(参考后文)保留，并且保留IO_CACHE中的分配的内存空间，和物理文件描述符
6. 客户端断开连接，这个过程会释放IO_CACHE同时释放其持有的binlog cache内存空间以及持有的binlog 临时文件。 本文主要关注步骤3和4过程中对binlog cache以及binlog 临时文件的写入细节。

# MySQL的BinLog

在MySQL中，除了存储引擎InnoDB的日志外，自身还有一个BinLog，其中记录了对数据进行了修改（以及可能对数据有修改）的事件，主要用在主从复制和PITR场景中。

> BinLog除了记录修改数据的操作，还会记录元信息：
>
> - 确保操作重放的正确的额外信息
> - 错误码
> - binlog自身的维护信息，比如rotate事件。
>
> 除了Redo、undo、binlog这些用于恢复的日志，MySQL中还有一些用于记录的日志：Errorlog、General Query Log、Slow Query Log、DDL Log。

每个binlog的大小由`max_binlog_size`决定，当超过该参数大小，会切换到一个新的文件；注意，同一个事务的binlog应该放在一个binlog文件中，当存在一个很大的事务，可能会超过`max_binlog_size`。

在启动的时候，由binlog-format指定三种类型：statement、row、mixed。

- statement：记录主上执行的语句
- row：以主键为标识，记录相应行的变更。
- mixed：默认使用statement，当MySQL认为[某个语句不能完全复制](https://dev.mysql.com/doc/refman/5.7/en/binary-log-mixed.html)，升级为记录row。

## lossless semi-synchronous

TODO

# Group Commit

在PostgreSQL中有一个参数[commit_delay](https://www.postgresql.org/docs/current/runtime-config-wal.html#GUC-COMMIT-DELAY)代表PostgreSQL中的组提交，即，相比于每个事务都刷盘，打开这个参数，PostgreSQL会等待一组事务然后一起提交；但并发高的时候能提高整体的吞吐量，但是在并发较低的时候。

当然，前提是PostgreSQL打开了`fsync`参数；另外考虑到并发低的时候，没有必要等待；这里当db的活动事务大于`commit_siblings`时，才会delay commit(group commit)。

同样地，在MySQL中为了解决fsync带来的tps吞吐瓶颈问题，也有Group Commit特性，但是MySQL中由于存储的redolog和上层的binlog需要保证XA一致性，因此实现起来相对复杂。

## binlog与redolog的一致性

InnoDB是MySQL默认支持的事务型存储引擎，当上层同时启用binlog时，为了保证上下日志的一致，需要采用XA 2pc进行两阶段提交。

1. Prepare InnoDB

   1. 在InnoDB的logbuffer中，写入prepare记录；
   2. **fsync**：日志文件刷盘；
   3. 获取`prepare_commit_mutex`。

2. Prepare binlog

   1. 没什么特别需要做的

      > **XA recovery**
   >
      > 如果这里binlog刷盘了，那么恢复的时候，该事务认为是提交的；如果在这之前crash了，该事务会被回滚。
   
3. Commit binlog

   1. 将事务写入binlog；
   2. **fsync**：基于`sync_binlog`配置的行为，进行binlog刷盘。

4. Commit InnoDB

   1. 在logbuffer中，写入commit记录；
   2. 释放`prepare_commit_mutex`;
   3. **fsync**：日志文件刷盘
   4. 释放InnoDB的锁

在多个事务并发的进行2PC提交的时候，redolog的写入顺序和binlog的写入顺序可能不一致；基于`prepare_commit_mutex`，确保三次刷盘操作的顺序，从而保证binlog与InnoDB的redolog的顺序是一致的。

> MySQL的主从复制一般基于binlog实现；

然而，由于存在这样一个互斥同步，导致第2步的binlog**不能进行组提交**；并且理想情况下，一个事务，只有一个fsync操作即可，然而这里进行三次，**性能上也不太乐观**；没有对此进行优化的之前，binlog除了作为存储模块还是事务调度模块，**模块不清晰**，不易于维护。

## binlog的group commit

上述基于`prepare_commit_mutex`的commit机制的临界区过大，使得整个并发度降低，从而整体的吞吐降低。

因此，将binlog的提交划分为三个阶段，每个阶段都有一个执行队列：

- flush：write
- sync：刷盘
- commit：成功

进入某阶段的第一个thread作为leader，后续进来的都是follower；leader将该阶段的threads注册到下一阶段中，然后统一负责处理该阶段的任务（如果下一阶段不为空，那么该leader成为下一阶段的follower，因此最慢的sync阶段会累积很多任务），此时follower就是等待处理完成的通知；

```bash
8.0

cmake . -DFORCE_INSOURCE_BUILD=1

make

/Users/liuyangming/src/mysql-8.0.16/runtime_output_directory
```



## 参数

- slave_exec_mode

▪ binlog_format=ROW
▪ binlog_rows_query_log_events=ON
▪ “mysqlbinlog –vv --base64-output=decode-rows” can print original statements
▪ binlog_row_image=FULL
▪ slave_type_conversions=ALL_NON_LOSSY
▪ log_only_query_comments=OFF (FB extension)
▪ slave_exec_mode=SEMI_STRICT (FB extension)
▪ log_column_names=ON (FB extension)
▪ Changes binlog format
▪ slave_run_triggers_for_rbr=YES (MariaDB extension, FB backported)
▪ sql_log_bin_triggers=OFF (MariaDB extension, FB backported)

TODO：https://dev.mysql.com/doc/internals/en/event-header-fields.html

