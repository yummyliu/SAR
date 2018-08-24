---
layout: post
title: 初识PostgreSQL的High Availability
date: 2018-08-13 11:04
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - PostgreSQL
typora-root-url: ../../yummyliu.github.io
---
> * TOC
{:toc}

# HA

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

+ 消息传输：Corosync 
+ 机器管理：Pacemaker
+ 应用适配器脚本：resource agents 
+ fencing适配器：fence agents 
+ 配置以上模块用到的客户端：pcs



# 方案0. Keep a DBA on duty

DBA 24*7 守在DB身旁，以备不测😂；

# 方案1. keepalived-repmger

###### 图1 keepalived-repmger中的故障切换逻辑

![image-20180813111059882](/image/image-20180813111059882.png)

当master故障时，keepalived将vip切换到一个hot standby上。并且该hot standby的VRRP协议中的角色切换为master，并自动启动notify_master脚本，将这个hot standby提升为master。

为了防止脑裂，集群中必须有一个 **shared witness server** 。其做出决定并将故障转移到较高优先级的从上。**witness server**确保某一网段有较高优先级，这样其他server不会自己promote。

# 方案2. HAproxy-Pgbouncer

###### 图2 Haproxy-Pgbouncer的故障切换逻辑

![image-20180813112113584](/image/image-20180813112113584.png)

通过这个架构，可以做负载均衡，提高整体带宽，资源利用率和响应时间，避免单一节点过载。通过冗余提高可用性和可靠性。

这个方案中，当前端haproxy1挂掉后，Keepalived将vip迁移到haproxy2上。而当后端master挂掉后，repmgrd(replicaltion manager watch-dog)将hot standby提升为主。这一方案中同样加上shared witness server同样有意义。