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