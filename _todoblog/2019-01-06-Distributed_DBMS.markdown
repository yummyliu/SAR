---
layout: post
title: 
date: 2019-01-06 11:25
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---
> 一开始写这个博客的初衷是在校期间的学习笔记，可以看到每篇博客都特别凌乱且没有章法；不过这一年来好多了，但是每次翻看之前的blog，我自己都不想看下去；主要的原因是觉得也没人看，不过最近发现还是有读者的，决定之后认真对待每一个blog。2019年开年第一篇blog，这里要立一个flag——以后的每一篇blog要有明确的主题并且阐述到位，确保搜索到这篇文章的人能够在页面上停留大于5分钟，不能仅仅当做一个凌乱的笔记。
>
> ![image-20190106114108370](/image/dau.png)



近些年随着大数据的火热发展，作为互联网数据的载体，数据库的的概念层出不穷，不管是NoSQL还是NewSQL，或者面向OLAP还是OLTP的。本文主要阐述分布式数据库的若干概念，希望看到这篇文章的你，如果希望了解这个方向，能够在读完有所收获（本文仅代表个人观点，要带着批判的眼光来看待）。

本文分为以下几个内容：

* TOC
{:toc}
# Why Distributed

| 考虑因素 | 单机                             | 分布式                 |
| -------- | -------------------------------- | ---------------------- |
| 性能瓶颈 | 单机的资源上限：CPU核数/磁盘带宽 | 通信代价               |
| 可用性   | 单点问题                         | 多活副本               |
| 伸缩性   | 受限于单个机器                   | 集群理论上没有伸缩上限 |

# Distributed Problems

老生常谈的CAP理论，是分布式系统基石。当你将

## CAP理论

## 分布式事务

### 2PL

### 无锁的2PL

#### Raft协议

### 分布式事务的（死）锁

#### 分布式死锁避免

#### 分布式死锁检测

## 分布式日志

## 向上透明

## 分布式的查询计划优化

![Distributed Query Processing Architecture](/image/distributed_query_architecture.png)

# AllStar of Distributed DBMS

## NoSQL



## NewSQL

