---
layout: post
title: 
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
---

##查询语句方面的优化：

1. 优化子查询，用join替换
2. 添加索引

##数据库结构方面的优化：

1. 拆表，讲查询中不常用的字段拆出来
2. 对于联合查询，建立中间表
3. 增加冗余字段

##插入记录的速度优化：

1. 插入大量数据时候，先删除索引，完成后再重新创建
2. 禁止自动提交
3. 使用批量插入，一条insert into 可以插入多条记录
4. 使用copy语句导入数据
