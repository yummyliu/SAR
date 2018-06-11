---
ayout: post
title: 
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PG
---

> PG9.4中，加入的新功能。维护着当前的复制状态（比如，复制完成的位置），即使slave下线或者断连了，还是保持着，以备其再次使用；
>
> 并不默认启用，这篇文章解释了它干了什么事，为什么在流复制中很有用，在逻辑复制中也很有必要；

当前流复制的工作方式中，master并不为落后的standby保留额外的wal；如果standby落后太多了，master仍然会删除wal，其中可能有standby需要replay的wal。standby会出现ERROR：

```
ERROR:  requested WAL segment 00000001000000010000002D has already been removed
```

为了避免这个，你可以配置master来持续归档，并且在slave中提供一个restore_command来获取归档中的wal，或者肯去重做一个slave。

另一个不太可靠的方案就是将`wal_keep_segments`设置的足够大，保证slave从不落后太多；但是，没有什么正确的方式来估算多高算高，而且，这个值设的越高，masterd的pg_xlog有可能空间不足。因此，归档是可靠的方案；

当slave连接的时候，master会保留用来复制的wal；当slave断开，master就做这个工作了。但是短暂的断连时有发生，这意味着对于连接状态的slave，保持wal没有太大必要。

我们已经通过从master的持续归档中获得wal的方式，避免了这个问题。利用一些脚本，NFS，或者scp等技术，我们可以很好的解决这个问题。

在基于日志流的逻辑复制中，我们采用上述持续归档的方式规避这一问题，因为master必须读取并解码wal日志，然后发送给slave；这就不能只是发送一个简单的wal记录。由于只有master才有事务ID状态的记录以及oid等信息，所以slave不能读取一个简单的wal，来提取他们需要的信息。

和物理复制不同的是，逻辑复制中简单的重做一个落后的备份不太可行。在一些备份节点上，可能包含着自己的数据。在双向备份的情况下，备份DB可能包含一些还没同步过去的数据，这不不能简单的取消这个备份节点，执行一个新的`pg_basebackup`。

slot的引入可以解决的这个问题，即使slave断开连接，master就能持续的关注复制的状态。尽管这是为了逻辑复制添加的，但是在物理复制中也是有用的，因为master可以维护好所有slave需要的wal。这就不需要猜测`wal_keep_segment`。也不需要维护一个归档的系统来维护wal日志。只要pg_xlog的空间足够，那么master就能够保留好wal日志，知道slave上线来重做wal。

这个方式的缺点就是，当slave长期下线，可能会使wal无限增长。可是监控和调整pg_xlog的空间是一个繁忙PG的管理的必须的工作。

###### 额外的影响

1. 默认地，物理复制不会使用slots。不做其他改变的话，我们需要在上流的复制节点使用wal归档。

   我们通过在`recovery.conf`中设置`primary_slotname`，在物理复制中replication slot。这个物理复制节点，已经在master中通过`pg_create_physical_replication_slot()`创建了。

2. 在物理复制中使用了slot，我们就不需要使用wal归档来做复制了（除非你想使用PITR来恢复）。你不需要为多个slave维护wal。也不需要维护archive的存储。

3. 但是，你需要关注pg_xlog的空间。系统自动保留wal，你得确保系统有足够的空间。

4. 如果slave长期断开，你必须在master上，手动删除slot。

5. 有了slot，你需要监控slave的状态，因为精确及时的信息都在`pg_replication_slot`中。因此，你监控pg_xlog的空间和所有replication slot。

6. 任何`wal_keep_segments`降级为最小，来维护wal。

因此，在物理复制中，使用slot是一个权衡方案。你不需要管理archive，但是需要监控master系统的状态，避免master挂掉。

#### 逻辑复制

##### feature

+ 基于replication identity（通常就是主键和唯一键索引）来复制数据对象
+ 目标server可以写，可以使用不同的索引和安全策略
+ 与流复制不同的，逻辑复制可以跨版本支持
+ 逻辑复制基于事件过滤的复制
+ 比起流复制，逻辑复制很小的写放大
+ Publication可以有多个subscription
+ 基于较小数据集的复制，提供了存储灵活性
+ 比如基于触发器的方式，又很小的服务器负载
+ 允许并行publisher之间的并行流；
+ 逻辑复制可以用来升级

##### 使用情况

+ 将多个数据库合并到一个数据库，用来分析
+ 不同大版本PostgreSQL之间的数据复制
+ 将本机数据库的增量更新，发送给指定DB
+ 给不同用户组访问复制数据的权限
+ 多个数据库之间共享数据

LR的限制

+ 表名必须结构相同
+ 必须有主键或者唯一建约束
+ 双向复制不支持
+ 不复制DDL（schema）
+ 不复制Sequence
+ 不复制TRUNCATE
+ 不复制大对象
+ Subscription可以有更多的列，并且顺序可以不同，但是类型和列名必须相同；
+ 超级用户才有权限添加所有表
+ Can not stream over to the same host(subscription will get locked).

###### Questions

1. 订阅漂移


2. ERROR:  could not create replication slot "oauth2_access_tokens_sub": ERROR:  could not load library "/usr/pgsql-10/lib/pgoutput.so": /usr/pgsql-10/lib/pgoutput.so: undefined symbol: is_publishable_relation

   发布端的pg二进制我重新安装了，新安装了pgoutput和老的可能不兼容；重启了db解决
