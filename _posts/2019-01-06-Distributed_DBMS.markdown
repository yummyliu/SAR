---

layout: post
title: 分布式杂谈
date: 2018-11-06 11:25
header-img: "img/head.jpg"
categories: 
  - DBMS
typora-root-url: ../../yummyliu.github.io
---
* TOC
{:toc}
计算机系统的运行时由位于相同或不同空间上的进程集合构成，进程之间通过收发信息来通信。如果一个系统中**消息传递延迟相对于事件间隔不可忽略，**就可称之为**分布式系统**——《Time, Clocks, and the Ordering of Events in a Distributed System》。

当单机系统的发展遭遇摩尔定律的限制时，将数据（磁盘或者内存）或计算任务（CPU）进行拆分就是**分布式存储**或**分布式计算**系统。对于无状态的任务，可以简单的将单机并行，变成多机并行；但是如果将数据进行了拆分，那么就需要考虑数据的可用性、容错性和一致性。本文针对数据的分布式存储场景，梳理一下当前自己所了解的大概，如下是简单梳理了一个的脑图。

![image-20191218195019164](/image/1217-distribute-lock.png)

如果将分布式的逻辑下方到存储层，个人认为需要考虑三个问题：

1. Data Partition：数据的切分方式；可以垂直切分（vertical），比如将一个宽表的部分列，拆分为子表；也可以水平切分（horizontal），将较大的业务表按照一个或多个`distributed_id`进行分割。这样可以在业务代码中，自己对数据进行路由，然后进行处理。
2. Data Replica：为了保证数据的可用性，并提高读写并发度；通常会做多个数据副本，那么这些副本上的数据，需要保证数据一致，则需要考虑复制协议（Replica Control），可分为以下几种：
   + Primary Copy Replica Control，这是最常见的一种，即，一主多从的方式；可以进行读写分离，但是只有一个节点可写。
   + Read One/Write All Replica Control，在这种方式中，业务可以从最优的一个节点读取，但是写入的时候，需要在所有节点写入；这适合读远大于写的场景。
   + Quorum Consensus Replica Control，读写通过多数派决策，保证数据的一致性；
   + Group Replication，组复制；需要处理数据冲突的情况。

3. Transaction ACID，分布式环境中修改多个节点上的数据时，为了保证数据正确性，可引入分布式事务；那么就同样需要满足事务的ACID四个特性。

   + Atomic：通过两阶段提交，保证在所有节点上要么全成功，要么全失败。

   + Consistency：通过副本间数据一致性协议，保证单个操作的全局一致性；从而确保整体上的所有操作的一致性。

   + Isolation：对于多个事务的并发，同样可以通过锁或者MVCC的方式进行并发控制，对事务进行隔离；那么，就需要考虑分布式事务的死锁问题和全局事务ID（先后顺序问题，通过物理时钟或者逻辑时钟解决）。

     注意，这里的锁和单机情况类似，同样分为两个级别：可见事务锁和不可见的latch(rwlock/mutex)。

     > Global Isolation Level != All Local Isolation Level
     >
     >  Theorem: If all sites use a strict two-phase locking protocol and the transaction manager uses a two-phase commit protocol, transactions are globally serializable in commit order.

+ Durability：和单机环境类似，保证事务持久性，可通过全局日志解决。



从上节描述来看，在分布式带来了弹性，也带来了很多新的问题；比较关键的有三点，解决时序问题的分布式时钟，解决操作（日志记录）一致性问题的共识算法和解决整体数据正确性问题的分布式事务。那么，本位就是对这三个方面做一个笼统的介绍，希望对刚开始了解分布式环境的同学有所帮助。

##### 分布式时钟

首先，需要能够确定事务的先后顺序（transaction ordering），在《Time, Clocks, and the Ordering of Events in a Distributed System》论文中，详细阐述了逻辑时钟的实现协议，这里不做详细阐述。

##### 分布式一致性

###### CAP theorem

单机数据库通过本地事务来保证数据一致性。分布式系统的一致性是保证整个系统的各处的状态是相同的。对于无状态的分布式系统，系统间的协调几乎没有必要了；但是对于像数据库这种有状态的，为了对外表现的是一个整体，就需要在C/A/P之间权衡了（Principles of Distributed Computing ——Eric Brewer）。

+ （强）*Consensus(mutual consistency)*：确保客户端链接上每个分布式节点node，都是看到**相同的且最新的**数据；并且能够成功的写入。这种一致性是一种强的序列化的一致性。
  + – Strong: all replicas always have the same value • In every committed version of the database 
  + – Weak: all replicas eventually have the same value 
  + – Quorum: a quorum of replicas have the same value
+ （高）*Availability*：**每个**有效节点都能在**合理的时间**内响应读写请求；
+ *Partition Tolerant*：由于网络隔离或机器故障，将系统分割后，系统能够继续保持服务并且保持一致性；当分割恢复后，能够优雅的恢复回来。

> 这里的**CA**和ACID中的**CA**是两码事。A就不用说了，一个是可用性，一个是原子性。
>
> ACID中的C是Consistency，强调的是连贯性，前后一致。
>
> CAP中的C是Consensus，强調的是共识，各个节点之间是否达成一致意见。

由于CAP三者不能同时满足，从而有状态的分布式系统就分为了三种类型：

+ CP：当系统出现网络分区时，这时牺牲了可用性，保障整体一致性和分区容忍性。
+ AP：当系统出现网络分区时，这时牺牲了一致性，保证性能可用性和分区容忍性。
+ CA：如果单机的DB算一个分布式系统，那么就算一个CA的系统。但是，网络分布式系统中，由于node之间是通过网络进行通信的，网络分割是常有的事。分布式系统中一定要处理P这个问题，因此很少有分布式的CA系统。

所以，分布式系统一般就是在考虑在产生网络分区时，我们应该优先保证**强一致性**还是**完美的可用性**。但是一般我们是尽量两方面都做到尽量好。对于AP系统，一般是一些NoSQL系统，这种系统可以通过raft等**一致性协议**对多个读写操作的顺序进行协调，保证每个节点上的数据操作顺序是相同的，那么就能做到**最终一致性**。而对于CP系统，更加关注的是一致性，这里就利用**分布式事务**在整个系统之间进行操作的调度协调。这就到了本文要介绍的分布式事务了。

##### 分布式事务

对于本地事务来说，逻辑上是一个整体的一系列读写操作。 事务比较细致地可以区分为五种状态：

- active：正在执行某条语句
- partially commited：上一条语句执行成功
- commited：事务成功了
- failed：某条语句失败了
- aborted：事务失败了

###### 2PC

一个分布式事务可以看做是多个本地事务的按照某个协议的协同操作。常说的就是2PC协议，这是X/Open提出的通用的分布式事务协议。在XA协议中一般有两个角色，一个全局协调者的TM(Transaction Manager)与多个本地存储服务的RM(ResourceManager)。2PC的两个阶段如下：

![2PC protocol](/image/11203337-ptwo-phase-commit-protocol-1.png)

理想情况是：在voting阶段，如果RM节点返回了Yes；那么提交成功。否则，全部回滚。

如果某个RM节点在返回Yes之前挂了，那么TM可以感知到从而进行Rollback。如果在返回Yes之后挂了，那么此时这个全局事务同样标记为Commit；当挂掉的RM节点重启恢复的时候，本地发现还有未提交的Prepared的全局事务，此时会重新查询TM中全局事务的状态，来决定对其进行Commit还是Rollback。

> 在PostgreSQL和MySQL中都支持了XA协议的prepare等操作，需要注意在MySQL5.7之前的版本中，prepare操作不写binlog，因此如果MySQL5.6作为RM节点，宕机恢复时会有问题。

###### 3PC

2PC第一步等待回复时会阻塞操作，在3PC中，基于以下基本前提，可以解决2PC的阻塞问题。见如下从wikipedia盗的图。

> + 没有网络分区
> + 至少一个节点可用
> + 最多有K个节点同时挂机是可以接受的

![Three-phase commit diagram.png](/image/Three-phase_commit_diagram.png)

> 另外还有一种分布式事务的实现方式：TCC-柔性事务
>

不管基于什么协议实现的单个分布式事务，其保证了ACD特性，而作为一个真正的事务还需要满足并发环境的I（隔离性）。这就需要提到并发控制。

在多事务并行中，如果两个事务中的两个操作（这两个操作其中至少有一个是写）目标是同一个对象，那么会产生冲突。这里就要求并行调度保证事务前后的正确性，以及运行期间的隔离性。

###### 并发控制

在单机环境中，一般有三种方式进行并发控制：

+ MVCC：多版本并发控制。数据带上和事务标识相关的版本号。
+ S2PL：严格两阶段提交协议；比起2PL，S2PL直到事务结束才释放写锁。
+ OCC：乐观并发控制。在冲突较低的场景下，在事务结束才判断是否冲突，提高性能。整个事务就分为三个阶段：执行、确认、提交。在确认阶段有一些判断规则。

相应地，在分布式环境中有基于同样思想的并发控制：

+ Distributed 2PL：系统中有一个或若干个锁管理器节点，该节点负责全局的锁分配和冲突检测。
+ Distributed MVCC：这里需要有一个全局唯一的自增ID（或时间戳）；在Google的spanner中物理的方式实现了一个全局时间戳。另外，还有使用混合逻辑时间戳（CockroachDB）。
+ Distributed OCC：和单机环境相同。但是在确认阶段有一些分布式环境中相应规则。

并发控制的目标就是将同时进行的读写操作，最终整合成为一个串行化的操作，这样在顺序的redolog中就能有序摆放，进而故障恢复的时候也是很有秩序。那么如何达成这一目标呢？一般有两种方式：

不过是哪种方法，都是为了进行多个全局事务的读写同步；在**每个分布式的事务内部**还是按照多阶段的方式提交。

> 分布式死锁检测
>
> + 中心点集中检测，如果有一个全局锁服务，可以在该服务中，做死锁检测。
>
> + 每个节点单独检测，需要同步其他节点的事务依赖序列。