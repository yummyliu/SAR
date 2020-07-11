---
layout: post
title: RocksDB——Write Prepared
date: 2020-05-20 15:20
categories:
  - MyRocks
typora-root-url: ../../layamon.github.io
---
* TOC
{:toc}

# 2PC in RocksDB

在了解RocksDB的Write Prepared之前，还是得了解一下RocksDB的2PC。

<img src="/image/write-prepared/2pc-rocksdb.png" alt="image-20200522101817407" style="zoom:50%;" />

2PC只是针对PessimisticTransactionDB，通过Prepare(xid)/EndPrepare() 包含了xid事务的数据，最终Commit(xid)和Rollback(xid)标记了对应事务的状态。

和其他引擎类似，RocksDB事务写数据包含两部分：写日志和写数据；RocksDB的2PC写入策略默认是write_commited：

1. Prepare：此时只写日志，WriteImpl会在日志中插入meta marker——Prepare(xid)和EndPrepare()；如上图淡蓝色部分（改数据不存在与WriteBatch中，只是在调用相关接口时才追加到log中）。
2. 提交or回滚
   1. Commit：同样基于WriteImpl对象，此时日志中追加Commit(xid)标记，同时通过MemTableInserter将WriteBatch中的数据写入到对应CF的MemTable中。
   2. Rollback：此时清空WriteBatch中的数据，同时基于WriteImpl在日志中标记Rollback(xid)。

由此可以看出，write_commit方式的事务提交，MemTable中的都是提交的数据，判断事务可见性逻辑简单；但是commit阶段需要做的事情太多，成为系统吞吐瓶颈。因此，RocksDB提出了write_prepared的写入策略，带来的复杂性主要是判断数据记录（record）的可见性复杂了，原来MemTable中全是commit的数据，而现在既有Prepared也有commited。如下文。

# write_prepared相关结构（5.18）

要解决问题是：将写MemTable提前到Prepare阶段，带来的问题就是MemTable中数据上的seq，并不知道这些seq是否提交了？

因此，需要额外的结构记录当前系统哪些seq已经提交了，而当前活跃的seq区间很大，不可能全部记录状态；因此基于不同的数据结构在有限空间下解决这个问题。

```cpp
class WritePreparedTxnDB : public PessimisticTransactionDB {
  PreparedHeap prepared_txns_;
  std::unique_ptr<std::atomic<CommitEntry64b>[]> commit_cache_;
  std::atomic<uint64_t> max_evicted_seq_ = {};
  std::map<SequenceNumber, std::vector<SequenceNumber>> old_commit_map_;
  std::set<uint64_t> delayed_prepared_;

  mutable port::RWMutex prepared_mutex_;
  mutable port::RWMutex old_commit_map_mutex_;
  mutable port::RWMutex commit_cache_mutex_;
  mutable port::RWMutex snapshots_mutex_;
...
};
```

如下：

<img src="/image/write-prepared/structures.png" alt="image-20200522141733505" style="zoom:50%;" />

## prepared_txns_[min heap]

```cpp
PreparedHeap prepared_txns_;
```

事务Prepare时，即将Prepared seq插入到该堆中；Commit时从中删除。

通过该结构，开启SnapShot时，可以得知当前未提交事务的最小seq——`min_uncommitted_`，即`prepared_txns.top()`（待议）。

## commit_cache_[fixed-len array]

> like PostgreSQL clog

其是固定大小的数组，数组元素是**CommitEntry(prep_seq, commit_seq)** （下简称ps,cs）并且设计为[lock free commit cache](https://github.com/facebook/rocksdb/wiki/WritePrepared-Transactions#lock-free-commitcache)。

```cpp
struct CommitEntry {
  uint64_t prep_seq;
  uint64_t commit_seq;
}
std::unique_ptr<std::atomic<CommitEntry64b>[]> commit_cache_;
```

事务提交时：

1. 推进`max_evicted_seq_`
2. 在`commit_cache_`中记下<ps,cs>（此结构只保留最近提交的`CommitEntry`足够应对大多数情况）。

## delayed_prepared_[set]

```cpp
std::set<uint64_t> delayed_prepared_;
```

当`max_evicted_seq_`推进时，要从Commit cache中清除entry，如果ps还在`prepared_txns_`中，将其从`prepared_txns_`转移到`delayed_prepared_`中，如下：

```cpp
while (!prepared_txns_.empty() && prepared_txns_.top() <= new_max) {
  auto to_be_popped = prepared_txns_.top();
  delayed_prepared_.insert(to_be_popped);
  ROCKS_LOG_WARN(info_log_,
                 "prepared_mutex_ overhead %" PRIu64 " (prep=%" PRIu64
                 " new_max=%" PRIu64 " oldmax=%" PRIu64,
                 static_cast<uint64_t>(delayed_prepared_.size()),
                 to_be_popped, new_max, prev_max);
  prepared_txns_.pop();
  delayed_prepared_empty_.store(false, std::memory_order_release);
}
```

> Question_1：前面说到 `min_uncommitted_`由`prepared_txns_.top`得到，而由于`delayed_prepared_`的存在，`prepared_txns_.top`不能保证是当前最小未提交的seq，而实际的逻辑是：
>
> ```cpp
>       return std::min(prepared_txns_.top(),
>                       db_impl_->GetLatestSequenceNumber() + 1);
> ```

## old_commit_map_[map]

```cpp
std::map<SequenceNumber, std::vector<SequenceNumber>> old_commit_map_;
```

`max_evicted_seq_`推进时，某个获取SnapShot的长事务持有了**从Commit cache中请出但未提交的prepare seq**，将相应<snapshot seq, prepared seq>加到map中，见函数`CheckAgainstSnapshots`：

```cpp
  // then snapshot_seq < commit_seq
  if (prep_seq <= snapshot_seq) {  // overlapping range
    WPRecordTick(TXN_OLD_COMMIT_MAP_MUTEX_OVERHEAD);
    ROCKS_LOG_WARN(info_log_,
                   "old_commit_map_mutex_ overhead for %" PRIu64
                   " commit entry: <%" PRIu64 ",%" PRIu64 ">",
                   snapshot_seq, prep_seq, commit_seq);
    WriteLock wl(&old_commit_map_mutex_);
    old_commit_map_empty_.store(false, std::memory_order_release);
    auto& vec = old_commit_map_[snapshot_seq];
    vec.insert(std::upper_bound(vec.begin(), vec.end(), prep_seq), prep_seq);
    // We need to store it once for each overlapping snapshot. Returning true to
    // continue the search if there is more overlapping snapshot.
    return true;
  }
```

一般这个情况很少发生，除非发起了一个较大的读事务，比如备份，这时日志里会有如下类似的warning：

> warning : old_commit_map_mutex_ overhead for 1798790 commit entry: <1798784,1798794>

# 可见性判断：IsInSnapshot

```cpp
class SnapshotImpl : public Snapshot {
 public:
  SequenceNumber number_;  // const after creation
  // 用在WritePrepared中
  SequenceNumber min_uncommitted_ = 0;
}
```

RocksDB的非锁定读也是通过MVCC实现，在读取的时候开启一个快照，如上，其中有该快照获取时的seq和当时未提交事务的最小seq。

> 类似于lock free commit_cache，snapshot同样将一部分snapshot信息，放在一个固定的cache中，做一个[lock free snapshot list](https://github.com/facebook/rocksdb/wiki/WritePrepared-Transactions#lock-free-snapshot-list)
>
> ```cpp
>   static const size_t DEF_SNAPSHOT_CACHE_BITS = static_cast<size_t>(7);
>   const size_t SNAPSHOT_CACHE_BITS;
>   const size_t SNAPSHOT_CACHE_SIZE;
>   std::unique_ptr<std::atomic<SequenceNumber>[]> snapshot_cache_;
>   ```
>   

需要注意的时record中的seq是prepare_seq，最后比较也是比较prepare_seq与snapshot_seq的关系，commit信息存储在上面额外的结构中。

那么现在问题是：如何判断某个Prepared Seq标记的record（ps），对某个SnapShot Seq （ss)标识的快照是否可见？这相关的逻辑在`IsInSnapshot`中。

> 在判断可见性的过程中，可能对应的snapshot已经无效了，fix issue：[snap_released](https://github.com/facebook/rocksdb/commit/f3a99e8a4de2b0147df83344463eb844d94a6a35)

核心就是**判断prepared_seq对应的commited_seq是否小于snapshot_seq**，细节就不展开，网上有关于可见性详细的描述。

> Question 2：关于"TODO(myabandeh): include delayed_prepared_ in min_uncommitted" (已经fix了)
>
> 在`IsInSnapshot`，判断一个prep_seq标记的Record对snapshot_seq标记的snapshot是否可见。
>
> 由于min_uncommited的计算没有依赖delay_prepared_中的数据，如下，因此在判断prep_seq与min_uncommited关系之前，需先确认prep_seq不在delayed_prepared\_之中
>
> ```cpp
> // Note: since min_uncommitted does not include the delayed_prepared_ we
> // should check delayed_prepared_ first before applying this optimization.
> // TODO(myabandeh): include delayed_prepared_ in min_uncommitted
> if (prep_seq < min_uncommitted) {
> ```
>
> 首先判断获取snapshot的时候prep_seq是否提交的依据是：prep_seq不在delayed_prepared\_中并且prep_seq不在prepared_txns。
>
> + 获取snapshot的时候，`prep_seq`不在`prepared_txns`中，通过`snapshot->min_uncommited`判断；
>
> + 获取snapshot的时候，`prep_seq`不在`delayed_prepared`\_中，则没办法判断；因为`delayed_prepared_`是不断变化的，并且 **min_uncommitted does not include the delayed_prepared\_**。
>
> 那么，如果存在长事务（seqno = s1）在获取snapshot时未提交，并且已经被evict到delay中；那么**snapshot->min_uncommited = prepared_txn.top > s1**；
>
> 而后来在`IsInSnapshot`之前提交了，则移出delay；那么对于s1标记的record，满足**不在`delayed_prepared`\_中**且**s1 < min_uncommited**，则该snapshot误认为该record可见。