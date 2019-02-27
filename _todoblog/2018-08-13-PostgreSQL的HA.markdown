---
layout: post
title: PostgreSQL的HA初识
date: 2018-08-13 11:04
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - PostgreSQL
typora-root-url: ../../yummyliu.github.io
---

* TOC
{:toc}

# HA简介

> Anything that can go wrong will go wrong. 
>
> ​					——Murphy's law

常说的几个9的可用性，就是（uptime/(uptime+downtime)）；

## 故障检测

关于故障检测我们需要考虑若干问题：

+ 主是否只是暂时阻塞了？
+ 判断错误之后，是否可以两个master同时运行？
+ 集群被分割成两部分，这两部分不可通，但是都和app可通，怎么办？
+ 如何避免重复failover？
+ 系统对于timeout如何处理的？

故障时的自动故障转移（failover），常用的是两个工具**pacemaker**和**Linux HA**。

## 脑裂

集群的可写主节点通常只有一个，由于某些原因导致集群中出现两个主节点，这时就需要quorum和fencing的技术。

### 仲裁（quorum） 

当集群被分割成两个互不相通的组时，需要一种机制决定那个组是最终的master，这就叫仲裁；比如加入另一个决策节点（tiebreaker ），但注意这有可能成为一个单点。

### 围栏（fencing）

仲裁只是failover的第一步，决定了哪个组是最终的master后，对于另一组，需要确保将其与现有环境隔离，或者说保证另一组彻底down掉；通常我们对这个过程称作：STONITH （shoot the other node in the head ）。有很多fencing机制，最有效的就是直接控制目标机器将其关闭。

## Linux HA技术栈

+ 消息传输：Corosync，通过 corosync-keygen 生成的一个共享key进行交流；
+ 机器管理：Pacemaker，基于Corosync进行仲裁，最终对集群进行failover操作。我们并不是告诉Pacemaker怎么做，而是告诉Pacemaker这个集群应该是什么样子？比如，应该启动哪些服务，这些服务在哪以及以什么顺序启动当出现故障时，Pacemaker重新制定计划其进行操作。
+ 应用适配器脚本：resource agents ，基于Open Cluster Framework (OCF) 编写的agent（通常就是shell脚本），对需要HA的资源的配置以及查询相关信息，官方站点有PostgreSQL的resource agents（PG RA）。
+ fencing适配器：fence agents ，对底层fencing命令的包装，比如，网络远程停止机器或通过hypervisor 重置VM等。
+ 配置以上模块用到的客户端：pcs，底层的配置都是在XML中写好的，直接改配置即可，但是这样不够友好。



# HA方案

## 方案0. Keep a DBA on duty

DBA 24*7 守在DB身旁，以备不测。。。；

## 方案1. keepalived-repmger

图1 keepalived-repmger中的故障切换逻辑

![image-20180813111059882](/../Desktop/repmger.png)

当master故障时，keepalived将vip切换到一个hot standby上。并且该hot standby的VRRP协议中的角色切换为master，并自动启动notify_master脚本，将这个hot standby提升为master。

为了防止脑裂，集群中必须有一个 **shared witness server** 。其做出决定并将故障转移到较高优先级的从上。**witness server**确保某一网段有较高优先级，这样其他server不会自己promote。

## 方案2. HAproxy-Pgbouncer

图2 Haproxy-Pgbouncer的故障切换逻辑

![image-20180813112113584](/../Desktop/haproxy.png)

通过这个架构，可以做负载均衡，提高整体带宽，资源利用率和响应时间，避免单一节点过载。通过冗余提高可用性和可靠性。

这个方案中，当前端haproxy1挂掉后，Keepalived将vip迁移到haproxy2上。而当后端master挂掉后，repmgrd(replicaltion manager watch-dog)将hot standby提升为主。这一方案中同样加上shared witness server同样有意义。

## 方案3. Pacemaker

### 简介

![image-20180829103926035](/../Desktop/pacemaker.png)

PaceMaker分为几部分：

###### 消息层：Heartbeat/Corosync

+ 节点的关系，以及节点的添加删除的周知；
+ 节点间消息传递
+ 仲裁系统

###### 集群资源管理器：PaceMaker

+ 存储集群的配置
+ 基于消息层，实现最大资源利用率
+ 扩展性：按照指定接口编写好相应的脚本，就能被PaceMaker管理。

###### 集群胶水工具

+ 除了传输消息（Corosync）和CRM（PaceMaker）之外的工具
+ 节点本地的与packmaker的资源代理器交互的资源管理器
+ 提供fencing功能的STONITH守护进程

###### 资源代理器

+ 管理集群资源的代理器
+ 提供一些管理操作：start/stop/monitor/promote/demote等
+ 有一些现成的代理器，如：Apache，PostgreSQL，drbd等

Pacemaker使用hostname辨别各个系统，可以使用DNS或者直接在/etc/hosts下配置（最好配置的短小精悍，比如：pros-db12）；不要在hostname中，使用primary/slave，这在failover后混淆。

## 方案4：Patroni

看了很多一些HA方案后，个人对这个更感兴趣。







### 参考文献

https://www.pgcon.org/2013/schedule/attachments/279_PostgreSQL_9_and_Linux_HA.pdf





