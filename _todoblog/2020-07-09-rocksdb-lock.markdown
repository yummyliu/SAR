---
layout: post
title: RocksDB Overview(2)——Lock Manager
date: 2020-07-09 21:00
categories:
  -
typora-root-url: ../../layamon.github.io
---
> * TOC
{:toc}

# Lock Of Transaction

## PessimisticTransaction

### TransactionLockMgr

#### TryLock

#### 



More often, a reader holds it indirectly through a data structure called `super version`, which holds reference counts for list of mem tables and a `version` -- a whole view of the DB.



DBImpl.mutex_

+ MemTable和SST Table的reference counts
+ 在compact finishing，flushing，memtable creating前后修改元数据
+ 调度writer



To solve this problem, we created a meta-meta data structure called “[super version](https://reviews.facebook.net/rROCKSDB1fdb3f7dc60e96394e3e5b69a46ede5d67fb976c)”, which holds reference counters to all those mem table and SST tables, so that readers only need to increase the reference counters for this single data structure. 

> https://rocksdb.org/blog/2014/05/14/lock.html



```cpp
ReturnAndCleanupSuperVersion
```

> vs Version

#### 

## OptimisticTransaction

# Lock Of Thread

