---
layout: post
title: redis-cluster初识
date: 2017-07-17 23:35
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - Redis
---

## 特点

无中心结构, 采用hash槽的方式来分配数据，而不是一致性hash
分区容忍性: 预先分配slot，有16384个hash槽，通过crc16校验后对16384取模，当发生节点增减的时候，调整slot的分布即可。
可用性：Master-slave
一致性（consistency）： 最终一致性，采用gossip协议

## 配置文件

redis.conf  nodes.conf

## hash tag

redis-cluster 只能接受单个key的查询，多个key的话，必须是相同的hash tag


## smart client

client 必须能够处理 ask redirection,

## Cluster bus port

每个节点除了一个接收请求的端口外，还有一个内存信息传输的接口成为Cluster bus port

## slavenodes scaling reads

READONLY命令
