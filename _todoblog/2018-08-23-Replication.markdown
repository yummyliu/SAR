---
layout: post
title: 
date: 2018-08-23 11:02
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - PostgreSQL
  - System
typora-root-url: ../../yummyliu.github.io
---

> * TOC
{:toc}

> some nice-looking marketing papers of some commercial database vendors might leave you with the impression that everything is possible and easy to do, without
> any serious limitation.

# CAP

> Eric Brewer 2000

随着NoSQL的发展，这个理论已经成为分布式系统的基石。

+ Consistency : 同一时刻集群的所有节点能看到相同的数据；区别于ACID中的Consistency，在事务的C中，意思是事务将DB从一个合法的状态变到另一个合法的状态，合法是指符合定义的各种rule（约束条件，trigger，cascade等）。
+ Availability : 任何时间的读写都是成功的，即，任何节点任何时刻都是可用的。
+ Partition tolerance : 当集群的某一分区的节点不可访问时，系统仍然可以工作；可以理解为某一节点不能发出/收到任何消息了，即，可以理解为被网络隔离了。

三者不可兼得，在PostgreSQL，Oracle，DB2等数据库中提供的是CA的方案；然而，在NoSQL系统中，比如MongoDB和Cassandra中提供的是AP的方案，在这些系统中的一致性往往是最终一致性。

##### latency vs bandwidth

我们都知道光的传播是有上限的，假设CPU时钟是3GHz，那么一个时钟周期里，光就能传播10cm; 所以虽然可以通过并发，提高bandwidth，latency是必然存在的；；

##### Synchronous vs asynchronous replication

##### Single-master vs multimaster replication

##### Logical versus physical replication
> Some systems do use statement-based replication as the core technology. MySQL, for instance, uses a so-called bin-log statement to replicate, which is actually not too binary but more like some form of logical replication. Of course, there are also counterparts in the PostgreSQL world, such as pgpool, Londiste, and Bucardo.

逻辑复制区别于语句复制（比如now()）, 设置相对麻烦，但是很灵活；
物理复制常用来，备份整个集群，从而扩展集群。

# 分片（sharding）和数据分布

sharding是系统扩展的常用方式。

pros:

- It has the ability to scale a system beyond one server 
- It is a straightforward approach 
- It is widely supported by various frameworks 
- It can be combined with various other replication approaches 
- It works nicely with PostgreSQL (for example, using PL/Proxy) 

Cons:

- Adding servers on the fly can be far from simple (depending on the type of partitioning function) 
- Your flexibility might be seriously reduced 
- Not all types of queries will be as efficient as they would be on a single server 
- There is an increase in overall complexity of the setup (such as failover, resyncing, maintenance and so on) 
- Backups need more planning 
- You might face redundancy and additional storage requirements 
- Application developers need to be aware of sharding to make sure that efficient queries are written 

分片是扩展系统的第一步而已，对于多个表的系统，仅仅分片是不够的。比如在数据仓库中可以将事实表，按照id进行分片，但是对于维表就需要在每个节点上冗余存储。

其实在每个node上的分片方式有多种，上面提高的sharding是按照一个取余的映射关系，还有range的方式（比如，按照时间分区的方式分布），另外在NoSQL系统中，经常采用经常采用一致性Hash（Consistent hashing ）的方式分布数据。可以很方便的对集群进行扩容缩容（当然在PostgreSQL中也可以使用）。

## Consistent hashing 

有N个服务器，但是分配M（M>N）个虚拟节点，构成一个环；将数据按照M进行映射，实际的位置换在M前向的第一个实节点中。

## replication

> One is none and two is one. 

永远要有备份，没有备份的裸奔。。。