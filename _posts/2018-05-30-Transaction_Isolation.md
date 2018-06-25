---
layout: post
title: 事务隔离级别
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
---

SQL标准定义了四个级别的事务隔离。 最严格的是Serializable，它是由一个上下文中的标准定义的，任何一个Serializable事务的并发执行都能保证产生和按某一顺序一次一个地执行它们相同的效果。 另外三个层次是根据现象来定义的，这是由并发事务之间的交互产生的，而这并不是每个层次都会发生的。 标准指出，由于Serializable的定义，在这个级别上这些现象都是不可能的。 （这并不奇怪 - 如果交易的效果必须与一次运行一致，那么您怎么看到交互造成的现象？）

被各个级别屏蔽的现象有：

+ 脏读：

  事务读到了另一个事务未提交的数据

+ 不可重复读：（对单个行的读取，其他commit的事务对该行是否能够修改）

  事务重新读取之前读过的数据时，发现该数据被另一个提交的事务修改了

+ 幻读：（对整个表的查询，其他commit的事务是否对整个表进行了操作）

  事务重新执行一个查询时，返回一组满足查询条件的数据行，但是由于最近的另一个提交事务，返回的结果数据行发生了改变。

+ 串行化异常：

  一组成功提交的事务的结果，和以任何串行的顺序提交的事务的结果都是不同的。

SQL标准和PostgreSQL中实现的事务隔离级别比较：

（Allowed，But not in PG：SQL标准中，当前隔离级别下可能发生，但是PG中不会发生）

（Possible：SQL标准中和PG中都可能发生）

（Not Possible： SQL标准和PG中都不可能发生）

| 隔离级别 | 脏读                    | 不可重复读        | 幻读                    | 串行化异常        |
| ---- | --------------------- | ------------ | --------------------- | ------------ |
| 读未提交 | Allowed，But not in PG | Possible     | Possible              | Possible     |
| 读已提交 | Not Possible          | Possible     | Possible              | Possible     |
| 重复读  | Not Possible          | Not Possible | Allowed，But not in PG | Possible     |
| 串行化  | Not Possible          | Not Possible | Not Possible          | Not Possible |

在PG中，开启一个事务可以请求上述四种隔离级别(SERIALIZABLE | REPEATABLE READ | READ COMMITTED | READ UNCOMMITTED); 但是RU这个级别，其实就是RC。这是将标准隔离级别映射到PG的MVCC的明智方式。

在PG里，重复读同样避免了幻读；SQL标准允许严格的行为，四个隔离级别只是定义个哪些现象不能发生，没有说哪些现象一定发生。

**NOTE**：在PG里一些数据类型和函数有特殊的事务行为，特别地，对一个sequence的修改，其他事务里面可见，当前事务意外终止，也不会回滚。



