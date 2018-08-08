---
layout: post
title: 认识事务的隔离级别
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL

typora-root-url: ../../yummyliu.github.io
---

> * TOC
> {:toc}

### ANSI SQL的隔离级别

ANSI SQL 为了预防三种不同的异常现象，定义了四个隔离级别。

| 隔离级别 | 脏读                   | 不可重复读   | 幻读                   | 串行化异常   |
| -------- | ---------------------- | ------------ | ---------------------- | ------------ |
| 读未提交 | Allowed，But not in PG | Possible     | Possible               | Possible     |
| 读已提交 | Not Possible           | Possible     | Possible               | Possible     |
| 重复读   | Not Possible           | Not Possible | Allowed，But not in PG | Possible     |
| 串行化   | Not Possible           | Not Possible | Not Possible           | Not Possible |

SQL标准定义了四个级别的事务隔离。 最严格的是Serializable，即，任何一个Serializable事务的并发执行都能保证产生和**按某一顺序串行**地执行它们相同的效果。

另外三个级别是根据现象来定义的，这是由**并发事务之间的交互**产生的，而这并不是每个层次都会发生的。 由于Serializable的定义，在这个级别上这些现象都是不可能的。 （这并不奇怪：如果交易的效果必须与一次运行一致，那么您怎么看到交互造成的现象？）

### **并发**异常

#### 脏读：

事务读到了另一个正在运行的事务未提交的数据；

| T1                                | T2                             |
| --------------------------------- | ------------------------------ |
| select a from t where b=3;        |                                |
|                                   | update t set a = 5 where b =3; |
| select a from t where b=3;(a = 5) |                                |
|                                   | Rollback;                      |

#### 不可重复读

事务重新读取之前读过的数据时，发现该数据被另一个**提交的事务update**了；脏读和不可重复很像，区别在于脏读在T2还未提交时，就读了。

| T1                                | T2                             |
| --------------------------------- | ------------------------------ |
| select a from t where b=3;        |                                |
|                                   | update t set a = 5 where b =3; |
|                                   | commit;                        |
| select a from t where b=3;(a = 5) |                                |

在基于lock的并发控制中，t1在读取事务时没有获取读锁或者第一个select后，立马就释放了读锁；会导致t2读取到更新已经读过的但没提交的数据。解决方法就是在RR级别中，通过锁，使t1/t2进行串行化调度。

在基于mvcc的并发控制中，当t2发生**提交冲突**时，处理策略不够严格也会导致不可重复读。解决方法就是在RR级别中，通过只读取事务开始时版本的数据，保证在整个事务生命周期内数据是一致的。

> 提交冲突（commit conflict）:
>
> 为了避免上述基于锁的并发控制中，t1/t2要串行化才能解决问题，mvcc通过多版本数据，允许t2先提交，这样读写不阻塞；
>
> + 如果**在SERIALIZABLE级别中**，由于读的数据是**事务**开始时的快照，**t1只有读**不会出现问题；如果t1也对数据进行了变更，那么提交的时候，db会检查t1提交的结果和按照t1/t2（即，以事务开始时间算）顺序的串行结果是否一致，一致可提交，否则就是**提交冲突**；此时，t1需要回滚（这就发生了序列化错误）。
> + 或者**在RC中**，由于读的数据是**查询**开始时的快照，同时也没有串行化保证，也不会有提交冲突。

#### 幻读

由于另一个事务**Insert**了一条新数据，导致这个旧事务的某些查询结果变了，比如count(*)。

| T1                                                     | T2                              |
| ------------------------------------------------------ | ------------------------------- |
| select * from t where b >0 and b <5; （假设有2条数据） |                                 |
|                                                        | insert into t(a,b) values(1,3); |
|                                                        | commit;                         |
| select * from t where b >0 and b <5; (多了一条)        |                                 |

在基于锁的并发控制中，采用RR，意味着会将读出的数据行加锁。但是不会在表的某一范围（比如例子中只会对2条数据加锁）加锁，这样别的事务会在该范围内插入一条数据；后面同样的查询就会多出一条来。

#### 串行化异常

一组成功提交的事务的结果，和以任何串行的顺序提交的事务的结果都是不同的。

### SQL标准和PostgreSQL中实现的事务隔离级别比较


在PG中，开启一个事务可以请求上述四种隔离级别(SERIALIZABLE,REPEATABLE READ, READ COMMITTED , READ UNCOMMITTED); 但是RU这个级别，其实就是RC。在RR级别中，也不会有幻读现象。SQL标准定义了四个隔离级别只是定义个哪些现象不能发生，没有说哪些现象一定发生。

> **NOTE**：在PG里一些数据类型和函数有特殊的事务行为，特别地，对一个sequence的修改，其他事务里面可见，当前事务意外终止，也不会回滚。

### 隔离级别外传

另外，除了标准SQL中定义的隔离级别，在不同的db中，也衍生出比较特性的隔离级别，感兴趣可以看论文“ A Critique of ANSI SQL Isolation Levels”。

![img](/image/ansi-sql-isolation-levels.png)

###### 图1 隔离级别 from A Critique of ANSI SQL Isolation Levels

##### 参考文献

[wiki](https://en.wikipedia.org/wiki/Isolation_(database_systems)#Dirty_reads)

[ A Critique of ANSI SQL Isolation Levels](https://arxiv.org/pdf/cs/0701157.pdf)

