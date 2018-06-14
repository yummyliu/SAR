---
layout: post
title: PostgreSQL浅见
subtitle: 作为DBA的第六个月，以PostgreSQL为例，梳理一下若干概念
date: 2018-06-14 13:46
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - DB
    - PostgreSQL
---

> 从一开始的仓颉造纸，到最后的磁盘上的01串，存储媒介不断发生变化；而当进入信息时代，如何有效的在计算机中管理数据，最后就是数据库管理系统的任务了；而数据库就是解决两个问题：存储和计算；这两个任务能够有效的做好，完备的监控是必须的；
>
> 本文从三个角度，来概述PostgreSQL
>
> 1. 有效的存储
>    1. 存储介质
>    2. 存储结构
>    3. 存储冗余
> 2. 高效的计算
>    1. 单个计算
>    2. 多个计算
> 3. 完备的监控
>    1. 当前的状态
>    2. 历史的状态

## *有效*的存储

> 先把数据放好

### 存储介质

###### 内存

###### SSD

###### 磁盘

### 存储结构

#### 内存里的结构

##### PostgreSQL 内存划分

###### shared memory

+ shared buffer pool
+ WAL buffer
+ Commit LOG

###### backend process

+ temp_buffers
+ work_mem
+ maintenance_work_mem

##### PostgreSQL 内存管理

###### writer process

###### checkpoint process

#### 磁盘里的结构

##### PostgreSQL 目录结构

>  以PG10为例

##### PostgreSQL 表文件内部组织

###### 堆表 vs 索引组织表

###### 页结构

##### PostgreSQL 表文件内部管理

###### autovacuum process

### 存储冗余

#### 物理冗余

##### PITR

###### basebackup

###### archiver process

##### 流复制

###### wal sender process

###### wal receiver process

#### 逻辑冗余

##### Logic Replication

###### publication

###### subscription

##### pg_dump

## *高效*的计算

> 再把数据拿出来

### 单个计算——SQL解析

##### Parser

###### ParserTree

##### Analyzer

###### QueryTree

##### Rewriter

###### Rules

##### Planer

###### PlanTree

###### stats collector process

###### ScanNode (e.g.) 

###### JoinNode (e.g.) 

##### Executor

###### Pull

###### Push

### 多个计算——并发控制

#### 锁

##### 锁级别

##### 如何利用锁

#### MVCC

##### 优点

##### 缺点

## *完备*的监控

> 有一个大局观

### 当前的状态——pg_catalog

对DB的各项指标有所了解，了解当下的与之前的；

### 历史的状态——日志

###### logger process

##### 监控指标