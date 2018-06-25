---
layout: post
title: PostgreSQl的VACUUM与MySQL的Purge
subtitle: 因为实现多版本，需要保留一些旧版本的数据，不同的是保存的位置, 但是同样随时间增长，如果不及时清理，一个表占用的空间会膨胀
date: 2016-08-25 09:26
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - DataBase
---

### WAL日志

### Purge
在Mysql的InnoDB中，只有最新更新的行，才报错在表中。旧版本的行存储在回滚段中，但是删除的行是留在原地，并被标注为已删除。
因此，Purge可以去掉表中的已删除的行，并删掉回滚段中的旧版本的行。
用来找到删除行的信息，同样要保存在回滚段中。这样就很容易找到要被删除的行了。清除更新的行就很容易了，因为旧版本的都在回滚段中。
这一方式的一个小缺陷就是，执行一个update操作的就意味着要写两个tuple：old复制到回滚段。新的写在本地。

### VACUUM

PostgreSQL中没有回滚段表空间，或者其他类似的东西。
当更新一个行的时候；新的版本写在旧版本后面，旧版本也就留在原地。
和InnoDB类似，删除的记录同样被标记好，以待删除。
这一不同使得，执行的时候很简单了。但是在清理的时候，需要费点功夫。
没有集中管理需要删除的数据，PostgreSQL就需要全表扫描来清理。
但是在PostgreSQL8.3之后，有一个优化手段叫 HOT（heap only tuple）。允许清理工作在一个页增长的时候自动进行。而在8.4之后，系统维护一个bitmap，叫做visibility map
标记着那个page需要被清理，这样就只扫描这些page就行了。
无论如何，每个索引项还是需要全扫的，这样来说，Vacuum在Postgresql中是个昂贵的操作。

### 膨胀

因为实现多版本，需要保留一些旧版本的数据，不同的是保存的位置,
但是同样随时间增长，如果不及时清理，一个表占用的空间会膨胀。
比如存在一个长事务，或者Vacuum和Purge清理的速度跟不上。
在PostgreSQL8.3之后，VACUUM在后台开始使用多进程的方式来自动清理
Mysql的Purge是单线程，但是Percona Server等Mysql的其他分支，提供了多线程的方式

[ref](http://rhaas.blogspot.com/2011/02/mysql-vs-postgresql-part-2-vacuum-vs.html)
