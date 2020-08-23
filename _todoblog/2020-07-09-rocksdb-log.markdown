---
layout: post
title: RocksDB Overview(4)——Log Manager
date: 2020-07-09 21:00
categories:
  - rocksdb
typora-root-url: ../../layamon.github.io
---
> * TOC
{:toc}

# Log Manager

RocksDB的Log Manager有两类：

+ MANIFEST：VERSION
+ ***.LOG：kv pair的Write Ahead Log

## WAL

<img src="/image/rocksdb-log/logformat.png" alt="image-20200729180128159" style="zoom:50%;" />

每次当当前WAL对应的内存数据(memtable)刷新到磁盘之后，都会新建一个WAL，如下，并且还会考虑重用之前的wal。

```cpp
SwitchMemtable
	if (creating_new_log) {
      EnvOptions opt_env_opt =
          env_->OptimizeForLogWrite(env_options_, db_options);
      if (recycle_log_number) {
        ROCKS_LOG_INFO(immutable_db_options_.info_log,
                       "reusing log %" PRIu64 " from recycle list\n",
                       recycle_log_number);
        s = env_->ReuseWritableFile(
            LogFileName(immutable_db_options_.wal_dir, new_log_number),
            LogFileName(immutable_db_options_.wal_dir, recycle_log_number),
            &lfile, opt_env_opt);
      } else {
        s = NewWritableFile(
            env_, LogFileName(immutable_db_options_.wal_dir, new_log_number),
            &lfile, opt_env_opt);
      }
    }
```

RocksDB的wal也有类似[Group Commit的机制](https://github.com/facebook/rocksdb/wiki/WAL-Performance#group-commit)，可以提高整体速度。

![image-20200428141052855](file:///Users/bytedance/layamon.github.io/image/rocksdb/logs.png?lastModify=1595728616)

WAL按理说是顺序写的，每次只需要刷最后一个log就行了，为啥要foreach呢？logs_中保存的是还未sync的log，在commit之前，logwriter可能已经写了很多不同的log，需要一起刷盘。

> fsync需要两个io，一次是刷数据，一次是刷meta；为减少iops，使用Options.recycle_log_file_num预先分配好文件，这样只要文件不是写到最后，就不需要fsync meta

### LogRecord

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



### LogWriter



## Version

RocksDB是

<img src="/image/rocksdb/version.png" alt="image-20200709205542179" style="zoom: 50%;" />

+ last_sequence_：不使用two write queue的

+ last_allocated_sequence_：在wal中的记录，会分配seq，但是这些seq不会出现在memtable中。

+ last_published_sequence\_：当打开last_seq_same_as_publish_seq_后，改变量与last_seq相同，否则也会超前

  ```cpp
  // The 2nd write queue. If enabled it will be used only for WAL-only writes.
  // This is the only queue that updates LastPublishedSequence which is only
  // applicable in a two-queue setting.
  Status DBImpl::WriteImplWALOnly(const WriteOptions& write_options,
                                  WriteBatch* my_batch, WriteCallback* callback,
                                  uint64_t* log_used, uint64_t log_ref,
                                  uint64_t* seq_used, size_t batch_cnt,
                                  PreReleaseCallback* pre_release_callback
  ```

  

last_sequence <= last_published_sequence_ <=  last_allocated_sequence_



+ two write queue，原名叫 [concurrent_prepare](https://github.com/facebook/mysql-5.6/pull/763)，主要是针对writePrepared的事务的优化，在prepare阶段可以[ConcurrentWriteToWAL](https://github.com/facebook/rocksdb/commit/63822eb761a1c45d255e5676512153d213698b7c) 

### SuperVersion

compact结束 和 MemTableFlush时，会创建一个新的Version；Version-`current`表示最新LSM-tree中的文件。Get操作和Iterator的整个生命周期会基于current来读取数据。get/iter引用Version-current，每个version又引用各个文件；如果某个version没有被引用，就需要删除；如果某个文件没有被引用，也需要被删除。

- create version：对所有该version的文件 ref++；反之ref--

由于Version是共享的，ref变更需要加锁，为减少增减引用计数的锁代价，引入SuperVersion，这是thread local的变量。SuperVersion对MemTable和Sst都保持引用

```cpp
SuperVersion* s = thread_local_->Get();
if (s->version_number != super_version_number_.load()) {
  // slow path, cleanup of current super version is omitted
  mutex_.Lock();
  s = super_version_->Ref();
  mutex_.Unlock();
}
```

- create new MemTable
- flush or Compact完成

会创建新的SuperVersion；但这些事件的频率不高；在这之前get/iter引用thread local的sv进行查询。

之前，一个查询需要对MemTable和version分别取mutex，然后加引用；现在只去一次mutex，对SuperVersion加引用，并拷贝为自己的local。



> vs Version

#### 