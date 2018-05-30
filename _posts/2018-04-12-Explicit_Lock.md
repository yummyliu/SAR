---
layout: post
title:  "建jekyll+github博客(to be continue...)"
date:   2015-09-25 18:00
categories: jekyll update
tags:
    - PG
---

​PostgreSQL提供了各种锁定模式来控制对表中数据的并发访问。 锁可用于当MVCC不满足MVCC不满足需求时，由应用程序控制锁定。 此外，大多数PostgreSQL命令会自动获取适当模式的锁定，以确保引用的表不会在执行命令时，被冲突的事务删除或修改。 （例如，当同一个表上有其他操作在执行时，TRUNCATE不能同时执行，所以它会在表上获得排它锁后，执行该操作。）

​	使用pg_locks系统视图，可以看到现在系统中的锁。

####表锁

一些操作可以自动的获得一些锁，也可以用`LOCK`语句显示的获得某些锁。

+ ACCESS SHARE
  + 只读的查询在相应的表上，获得这个锁
  + ！EXCLUSIVE
+ ROW SHARE
  + SELECT FOR UPDATE/ FOR SHARE
  + ! EXCLUSIVE
+ ROW EXCLUSIVE
  + 修改表的操作
  + ！ SHARE, SHARE ROW EXCLUSIVE, EXCLUSIVE, ACCESS EXCLUSIVE
+ SHARE UPDATE EXCLUSIVE
  + VACUUM (without FULL), ANALYZE, CREATE INDEX CONCURRENTLY, ALTER TABLE VALIDATE and other ALTER TABLE variants
  + ! SHARE UPDATE EXCLUSIVE, SHARE, SHARE ROW EXCLUSIVE, EXCLUSIVE, and ACCESS EXCLUSIVE
+ SHARE
  + Create index； 阻止当前表数据的更改
  + ！ ROW EXCLUSIVE, SHARE UPDATE EXCLUSIVE, SHARE ROW EXCLUSIVE, EXCLUSIVE, and ACCESS EXCLUSIVE
+ SHARE ROW EXCLUSIVE
  + CREATE TRIGGER / ALTER TABLE
  + ! ROW EXCLUSIVE, SHARE UPDATE EXCLUSIVE, SHARE, SHARE ROW EXCLUSIVE, EXCLUSIVE, and ACCESS EXCLUSIVE lock
+ EXCLUSIVE
  + REFRESH MATERIALIZED VIEW CONCURRENTLY（锁是在视图上，还是在底层的表上？）
  + ！ ROW SHARE, ROW EXCLUSIVE, SHARE UPDATE EXCLUSIVE, SHARE, SHARE ROW EXCLUSIVE, EXCLUSIVE, and ACCESS EXCLUSIVE
+ ACCESS EXCLUSIVE
  + DROP TABLE, TRUNCATE, REINDEX, CLUSTER, VACUUM FULL, and REFRESH MATERIALIZED VIEW (without CONCURRENTLY)
  + 和所有的模式冲突，只能有一个事务访问当前这个表
  + tip：只有这个锁才能阻塞 select（without for update、share）查询

在一个事务中可以定义 很多savepoint，当rollback to 某个sp时，会释放该sp之后获得的锁。

####行锁

通过一些数据库操作自动获得一些行锁，行锁并不阻塞数据查询，只阻塞writes和locker。

+ FOR UPDATE
  + 会阻塞这些操作`UPDATE`, `DELETE`, `SELECT FOR UPDATE`, `SELECT FOR NO KEY UPDATE`, `SELECT FOR SHARE`or `SELECT FOR KEY SHARE`，防止这些操作 修改、删除、锁定这些行。
  + Delete某一行； Update某一行的某一列（该列上有唯一索引）；select  for update；这三中情况都会锁住相应的行
+ FOR NO KEY UPFATE
  + 比FOR UPDATE，级别低，不会阻塞其他 select for key share。
  + update的时候没有获得FOR update 锁的，会会获得这个锁
+ FOR SHARE
  + 和FOR UPDATE相似，但是获得的是SHARE锁，而不会EXCLUSIVE锁
  + 阻塞其他的`UPDATE`, `DELETE`, `SELECT FOR UPDATE` or `SELECT FOR NO KEY UPDATE`
+ FOR KEY SHARE
  + 和FOR SHARE相似，但是弱。

PG不再内存中，存储行的修改信息，因此同一时间修改的行数没有限制。因此，锁住一个行会导致磁盘写，select for update标记的row，这些行会开始写磁盘。

####页锁	

​	PG中，有对于表的数据页的共享/互斥锁，一旦这些行读取或者更改完成后，相应锁就被释放。应用开发者一般不用考虑这个锁。

####死锁

​	显式加锁会提高死锁的发生的概率。

​	` This means it is a bad idea for applications to hold transactions open for long periods of time (e.g., while waiting for user input).`

####咨询锁

​	当MVCC模型和锁策略不符合时，采用咨询锁。在表中存储一个标记位能够实现同样的功能，但是咨询锁更快，避免表膨胀，会话结束后能够被Pg自动清理。

获得咨询锁：

1. 会话级别：该级别获得的咨询锁，没有事务特征，事务回滚或者取消，之前获得的咨询锁不会被unlock，一个咨询锁可以多次获得，相应的要多次取消。
2. 事务级别：事务级别获得的锁，事务结束后会自动unlock。

两种方式获得的咨询锁，如果锁在了一个识别符上，那么他们也是互相block的。
