---
layout: post
title: 另一种存储引擎——LogStructure
date: 2018-09-19 17:13
header-img: "img/head.jpg"
categories: jekyll update
tags: 
  - LSM
  - Database
typora-root-url: ../../yummyliu.github.io
---

* TOC
{:toc}
# 数据库存储实现

我们常听说的存储方式是将数据存储在磁盘中，其实还有另一种存储方式是只存储日志记录。读取的时候，进行日志的合并操作。这一思想的实现方式就是SSTable（Sorted String Table）。

## SSTable

SSTable就是存储了海量的键值对，并这些键值对的读写吞吐量进行了优化。

# 例子

## LevelDB

## Oceanbase

### 设计目标

> 作为一个RDBMS主要有以下几个功能：
>
> - 链接管理与任务调度
> - 关系型查询
> - 事务型存储
>   - Access Methods
>   - Buffer Manager
>   - Lock Manager
>   - Log Manager
> - 准入控制和复制高可用等重要管理维护的功能，

通用的互联网应用（假设读多写少）的数据库，具备伸缩性、高性能、高可用和低成本的要求。

#### 满足ACID的分布式事务

PostgreSQL的多机事务是采用两阶段提交的方式实现的，这种两阶段提交时阻塞的；OceanBase基于Paxos分布式一致性协议实现的无阻塞的两阶段提交。

#### 高性能

读的高性能可以直接加一层cache，或者读的高性能可以直接用SSD；

特别是写的高性能，采用将随机写->顺序写提高（LSM Tree）；

#### 在线伸缩

实时伸缩的时间粒度可以到小时级别

#### 在线故障处理

auto failover；auto failure recovery;

### 架构

数据由baseline和increment组成；

#### 系统角色

increment存储在UpdateServer的内存的MemTable（多副本）中，组织形式类似于链表，执行写事务，并写Redo Log；

baseline存储在ChunkServer的磁盘（一般磁盘都是用SSD）的SsTable（多副本）中，组织形式为数据页，执行读事务。

除了以上两类服务器外，还有两类：

RootServer：总控管理，负载均衡；

MergeServer：真正处理Client的SQL请求的接口，支持JDBC/ODBC协议；

#### 系统读写

读通过避免SSD的随机写，充分利用SSD的随机读，来提供读的高性能；

随机写都在内存中进行，Redo Log是顺序写，并且一般这里使用的是带电池或电容的Raid卡，这种Raid卡带内存，提高写的速度。

#### 系统维护

#### 数据每日合并

UpdateServer每晚将MemTable和SsTable合并，并释放相应的内存和外存空间，这个过程叫做每日合并。

每日合并时，各个ChunkServer和UpdateServer数据可能不一致；从MergeServer发来的请求可能落在不同的数据上，但是不用担心数据的不一致性；因为数据是用baseline和increment构成，旧的数据是和旧的增量结合，新的和新的，因此最终是相同的。

#### DB高可用

一主二备

### 特点

+ 内外存混合使用的数据库，比外存型块，比内存型经济；

+ 保证 “强一致性”的前提 下，追求系统的“最大可用性”；
























