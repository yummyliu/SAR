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

To solve this problem, we created a meta-meta data structure called “[super version](https://reviews.facebook.net/rROCKSDB1fdb3f7dc60e96394e3e5b69a46ede5d67fb976c)”, which holds reference counters to all those mem table and SST tables, so that readers only need to increase the reference counters for this single data structure. 

> https://rocksdb.org/blog/2014/05/14/lock.html



```cpp
ReturnAndCleanupSuperVersion
```

> vs Version

#### 

# Log Manager

## logrecord

```
enum RecordType {
  // Zero is reserved for preallocated files
  kZeroType = 0,
  kFullType = 1,

  // For fragments
  kFirstType = 2,
  kMiddleType = 3,
  kLastType = 4,

  // For recycled log files
  kRecyclableFullType = 5,
  kRecyclableFirstType = 6,
  kRecyclableMiddleType = 7,
  kRecyclableLastType = 8,
};
```



可以重用旧的log文件，避免fdatasync去更新inode

```cpp
  size_t recycle_log_file_num = 0;
```



#### 2pc

https://github.com/facebook/rocksdb/wiki/Two-Phase-Commit-Implementation

#### Group Commit

https://github.com/facebook/rocksdb/wiki/WAL-Performance#group-commit

![image-20200428141052855](/image/rocksdb/logs.png)

WAL按理说是顺序写的，每次只需要刷最后一个log就行了，为啥要foreach呢？logs_中保存的是还未sync的log，在commit之前，logwriter可能已经写了很多不同的log，需要一起刷盘。

> fsync需要两个io，一次是刷数据，一次是刷meta；为减少iops，使用Options.recycle_log_file_num预先分配好文件，这样只要文件不是写到最后，就不需要fsync meta

#### WAL

#### Version

<img src="/image/rocksdb/version.png" alt="image-20200709205542179" style="zoom: 50%;" />

