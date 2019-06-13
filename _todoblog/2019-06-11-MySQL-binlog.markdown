---
layout: post
title: MySQL Binlog
date: 2019-06-11 18:28
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---

* TOC
{:toc}
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

```bash
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

+ binlog的切换时机：

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

查看binlog内容

```sql
mysql> show binlog events in 'bin.000001';
```

![image-20190612084052992](/image/binlog-example.png)

binlog是一组binlog文件加一个索引文件，包含了MySQL中的数据更改。

每个log文件由一个magic数(`#define BINLOG_MAGIC        "\xfe\x62\x69\x6e"`，即"\xfe bin")和一组event构成。

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

通过`show slave status`可以查看复制的进度。当SQL线程执行完某个relaylog上的所有event，那么就将该文件删除。

在如下三个情况下，MySQL slave会创建一个新的relaylog：

+ IO线程启动
+ FLUSH LOGS
+ 当前relaylog大于配置项([`max_relay_log_size`](https://dev.mysql.com/doc/refman/5.7/en/replication-options-slave.html#sysvar_max_relay_log_size))。

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

  ![Image result for little-endian](/image/bigLittleEndian.png)

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

TODO：https://dev.mysql.com/doc/internals/en/event-header-fields.html