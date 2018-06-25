---
layout: post
title: 对wal的操作一定要谨慎
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
---



```bash
 2014-10-16 13:15:28 IRST LOG:  could not open file "pg_xlog/00000001000007DC00000037" (log file 2012, segment 55): No such file or directory
 2014-10-16 13:15:28 IRST LOG:  invalid primary checkpoint record
 2014-10-16 13:15:28 IRST LOG:  could not open file "pg_xlog/00000001000007DC00000029" (log file 2012, segment 41): No such file or directory
 2014-10-16 13:15:28 IRST LOG:  invalid secondary checkpoint record
 2014-10-16 13:15:28 IRST PANIC:  could not locate a valid checkpoint record
```



## 安全的删除wal

如果想要删除wal日志，要么让pg在CHECKPOINT的时候自己删除，或者使用`pg_archivecleanup`。除了以下三种情况，pg会自动清除不再需要的wal日志：

1. `archive_mond=on`，但是`archive_command`failed，这样pg会一直保留wal日志，直到重试成功。
2. `wal_keep_segments`需要保留一定的数据。
3. 9.4之后，可能会因为replication slot保留；

如果都不符合上述情况，我们想要清理wal日志，可以通过执行`CHECKPOINT`来清理当前不需要的wal。

在一些非寻常情况下，可能需要`pg_archivecleanup`命令，比如由于wal归档失败导致的wal堆积引起的磁盘空间溢出。你可能使用这个命令来清理归档wal日志，但是永远不要手动删除wal段；

#### 从归档wal中恢复

可以手动的将wal从归档复制回pg_xlog/pg_wal中，或者创建一个recovery.conf其中配置好restore_command来做这件事。

#### 不需要归档的恢复

