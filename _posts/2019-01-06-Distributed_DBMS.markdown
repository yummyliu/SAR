---

layout: post
title: 分布式杂谈
date: 2018-11-06 11:25
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - DBMS
typora-root-url: ../../yummyliu.github.io
---
* TOC
{:toc}
# Why Distributed

首先什么叫分布式？顾名思义就是把目标分开布置。在计算机系统中，主要就是两个目标：存储和计算。所以我们常听说分布式存储，也听说了分布式计算。

那么为什么把一个整体分开呢？我总结的有以下几个原因：

| 考虑因素 | 单机                 | 分布式                                            |
| -------- | -------------------- | ------------------------------------------------- |
| 性能瓶颈 | 单机的资源上限       | 理论是节点存储和核数无上限，但是有通信代价。      |
| 可用性   | 单点问题             | 多活副本                                          |
| 伸缩性   | 受限于单个机器       | 集群理论上没有伸缩上限                            |
| 数据热点 | 全局数据共用一个缓存 | 不同业务将自己的数据放在离自己近的位置（如，DNS） |

# Distributed Problems

分布式有很多好处，同样也会存在问题；比如系统设计就会更加复杂、需要处理多节点之间通信的额外开销、很难保障多节点的数据完整性、以及多节点的数据分布方式会影响分布式查询的性能等等。每个方向都是一个课题，已经有很多前人进行了大量的研究，做工程时可以借鉴学习。

## 数据如何分布？

比如对于一个有N个字段的数据表，数据的分布可以从两个维度来考虑：

|                                                      | 有无副本 | 如何切分 |
| ---------------------------------------------------- | -------- | -------- |
| 拆库：将库中的某个表独立出来                         | Yes/No   | no       |
| 拆表：将表的若干字段独立成表                         | Yes/No   | 垂直     |
| 分区表: 按照某个distributeId，将大表分成若干个分区表 | Yes/No   | 水平     |

对于**有无副本**这个维度，不管是库级别的副本，还是表级别的或者是分区级别的，每一组副本都是只有一个leader，然后副本之间通过某种同步方式同步数据，然后读写分离。

## 数据一致性如何保证？

分布式系统的一致性是保证整个系统的各处的状态是相同的。对于无状态的分布式系统，系统间的协调几乎没有必要了；但是对于像数据库这种有状态的，为了对外表现的是一个整体，就需要在C/A/P之间权衡了——Principles of Distributed Computing ——Eric Brewer。

+ （强）Consensus：确保client链接上的每个node，都是看到相同的，最新的数据；并且能够成功的写入。这种一致性是一种强的序列化的一致性。

+ （高）Availability：**每个**有效节点都能在**合理的时间**内响应读写请求；

+ Partition Tolerant：由于网络隔离或机器故障，将系统分割后，系统能够继续保持服务并且保持一致性；当分割恢复后，能够优雅的恢复回来。

> 这里的**CA**和ACID中的**CA**是两码事。A就不用说了，一个是可用性，一个是原子性。
>
> ACID中的C是Consistency，强调的是连贯性，前后一致。
>
> CAP中的C是Consensus，强調的是共识，各个节点之间是否达成一致意见。

由于CAP三者不能同时满足，从而有状态的分布式系统就分为了三种类型：

+ CP：当系统出现网络分区时，这时牺牲了可用性，保障整体一致性和分区容忍性。
+ AP：当系统出现网络分区时，这时牺牲了一致性，保证性能可用性和分区容忍性。
+ CA：如果单机的DB算一个分布式系统，那么就算一个CA的系统。但是，网络分布式系统中，由于node之间是通过网络进行通信的，网络分割是常有的事。分布式系统中一定要处理P这个问题，因此很少有分布式的CA系统。

所以，分布式系统一般就是在考虑在产生网络分区时，我们应该优先保证**强一致性**还是**完美的可用性**。但是一般我们是尽量两方面都做到尽量好。对于AP系统，一般是一些NoSQL系统，这种系统可以通过raft等一致性性协议做到最终一致性。而对于CP系统，更加关注的是一致性，这里就利用分布式事务在整个系统之间进行操作的调度协调。

### 分布式事务

对于单个事务来说，其就是逻辑上是一个整体的一系列读写操作。 事务比较细致地可以区分为五种状态：

+ active：正在执行某条语句
+ partially commited：上一条语句执行成功
+ commited：事务成功了
+ failed：某条语句失败了
+ aborted：事务失败了

在多事务并行中，如果两个事务中的两个操作（这两个操作其中至少有一个是写）目标是同一个对象，那么会产生冲突。这里就要求并行调度是**可串行化的**。

#### 分布式事务并发控制

保证事务前后的正确性，以及运行期间的隔离性。

##### 并发控制的思想

在单机环境中，一般有三种方式进行并发控制：

+ MVCC：多版本并发控制。数据带上和事务标识相关的版本号。
+ S2PL：严格两阶段提交协议；比起2PL，S2PL直到事务结束才释放写锁。
+ OCC：乐观并发控制。在冲突较低的场景下，在事务结束才判断是否冲突，提高性能。整个事务就分为三个阶段：执行、确认、提交。在确认阶段有一些判断规则。

相应地，在分布式环境中有基于同样思想的并发控制：

+ Distributed 2PL：系统中有一个或若干个锁管理器节点，该节点负责全局的锁分配和冲突检测。
+ Distributed MVCC：这里需要有一个全局唯一的自增ID（或时间戳）；在Google的spanner中物理的方式实现了一个全局时间戳。另外，还有使用混合逻辑时间戳（CockroachDB）。
+ Distributed OCC：和单机环境相同。但是在确认阶段有一些分布式环境中相应规则。

##### 并发控制的方法

- 实现一个全局锁服务，比如Zookeeper这种，来进行全局操作的调度。
- 实现一个分布式一致性协议的库，各个节点基于同一个协议进行操作，这种要求程序员要熟悉Paxos或者Raft等衍生协议。

不过是哪种方法，都是为了进行全局的协调工作；协调的结果就是分布式的事务按照多阶段的方式提交。

#### 分布式锁

![image-20190123162322937](/image/image-20190123162322937.png)

##### 死锁检测

+ 中心点集中检测，如果有一个全局锁服务，可以在该服务中，做死锁检测。

+ 每个节点单独检测，需要同步其他节点的事务依赖序列。

#### 分布式一致性协议

+ Paxos：p2p的，没有leader；比如，zoonkeeper
+ Raft：有leader的；比如，etcd
+ Gossip：最终一致性；比如，consul

#### 分布式事务提交策略

保证所有数据副本的相互一致性（mutual consistency）。

多阶段提交就是为了保证所有副本都能知道其他副本对共享数据的操作的进度，进度同步后，才决定数据是在所有副本上都commited，或者全部aborted。

##### 2PL

事务提交分为两个阶段：vote、finish。

+ 在日志中写入启动事务记录，将操作发送到各个副本，并等待各个副本的回复。
+ 收集各个副本的回复，如果所有副本都是“ready to commit”，那么就发送一个commit消息到所有副本。所有副本提交，然后回复finish。

##### 3PL

2PL第一步等待回复时会阻塞操作，在3PL中，可以设置一个timeout进行处理，见如下从wikipedia盗的图。

![Three-phase commit diagram.png](/image/Three-phase_commit_diagram.png)

> 无锁的2PL: 另外还有基于一致性协议实现的无锁的2PL




