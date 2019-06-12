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

MySQL的配置文件

```bash
>  mysqld --datadir=/Users/liuyangming/dbdata/mysql/data --help --verbose | grep -A 1 'Default options'
Default options are read from the following files in the given order:
/etc/my.cnf /etc/mysql/my.cnf /usr/local/mysql/etc/my.cnf ~/.my.cnf
```

开启binlog的配置

```ini
[mysqld]
performance_schema=ON
log-bin=bin.log
log-bin-index=bin-log.index
max_binlog_size=100M
binlog_format=row
server-id=1
```

重启MySQL

```bash
mysqladmin -u root -p shutdown

/usr/local/mysql/bin/mysqld_safe --datadir=/Users/liuyangming/dbdata/mysql/data &
```

查看binlog

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

binlog的切换时机：

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

## BinLog代码模块

binlog中按照事务的先后顺序组织，每个事务分为若干个event。

client/mysqlbinlog.cc：解析binlog的工具。

sql/log.cc：该模块提供了宏观上操作的工具函数，比如创建/删除/写入binlog文件等。

sql/log_event.cc：底层操作log的函数，主要工作是将具体event序列化到record中。

sql/rpl_contants.h：INCIDENT_EVENT事件的代码

sql/slave.cc：slave端的IO和SQL线程处理binlog的代码

