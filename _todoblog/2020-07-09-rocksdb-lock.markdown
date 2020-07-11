---
layout: post
title: 
date: 2020-07-09 21:00
categories:
  -
typora-root-url: ../../layamon.github.io
---
> * TOC
{:toc}

### Lock Manager

#### TransactionLockMgr

#### SuperVersion

https://github.com/facebook/rocksdb/wiki/How-we-keep-track-of-live-SST-files

The list of files in an LSM tree is kept in a data structure called `version`. In the end of a compaction or a mem table flush, a new `version` is created for the updated LSM tree. At one time, there is only one "current" `version` that represents the files in the up-to-date LSM tree. New `get` requests or new iterators will use the current `version` through the whole read process or life cycle of iterator. All `version`s used by `get` or iterators need to be kept. An out-of-date `version` that is not used by any `get` or iterator needs to be dropped. All files not used by any other version need to be deleted. For example,

This logic is implemented using reference counts. Both of an SST file and a `version` have a reference count. While we create a `version`, we incremented the reference counts for all files. If a `version` is not needed, all files’ of the version have their reference counts decremented. If a file’s reference count drops to 0, the file can be deleted.

为减少增减引用计数的锁代价

```cpp
SuperVersion* s = thread_local_->Get();
if (s->version_number != super_version_number_.load()) {
  // slow path, cleanup of current super version is omitted
  mutex_.Lock();
  s = super_version_->Ref();
  mutex_.Unlock();
}
```



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

