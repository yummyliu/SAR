---
layout: post
title: 
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PG
---

# pg_statsinfo

间歇性地记录PG的活动和统计信息的snapshot，存储在另一个或者本dbserver上；另外还会在pg的csv格式的日志中提取活动信息；

##### 统计快照

默认是十分钟采一次snapshot；每个snapshot有以下的信息：

1. PostgreSQL的Statistics Collector采集的信息，比如：I/U/D以及buffer命中的统计；
2. 表空间，wal，archive的磁盘空间剩余；
3. 长事务以及对于的SQL
4. Session State统计
5. wal write rate
6. checkpoints和Vacuums的数量，执行时间和buffer命中统计；
7. 长查询及相应的查询，函数，查询计划
8. PostgreSQL的配置参数
9. OS参数：CPU Usage、 Memory Usage、Disk IO、load average
10. 长的锁冲突
11. 由于recovery导致的query cancel
12. Replication staus
13. 用户自定义的警告函数的警告信息
14. 利用SystemTap工具获取一些概述信息

每天按照10min频率采集信息，一天的数据量大概是有90~120Mb

##### 日志过滤

