---
layout: post
title: 
date: 2020-07-28 20:39
categories:
  -
typora-root-url: ../../layamon.github.io
---
* TOC
{:toc}
被写入的对象是WriteBatch，写入的对象是Wal+MemTable；在写入的过程中需要酌情为WriteBatch中的Key配上SequenceNumber。因此，本文主要解决两个问题：

1. WriteBatch是如何写入到DB中？——WritePath
2. WriteBatch中的Key怎么分配的SequenceNumber？——seq_per_batch or not?

# WritePatch

db_write_impl将write_batch写入到wal和memtable中。采用group commit的方式写。db中有一个writer_thread的队列，队列头部就是group leader，负责将队列中所有的batch写入到WAL和MemTable中；不管怎么写，writewal总是在memtableinsert之前，

![image-20200729095202413](/image/rocksdb-writeimpl/writebatch-dataflow.png)



SwitchMemtable：WAL与MemTable强绑定，只要有一个MemTable执行flush后，就换一个新的WAL；之后的数据继续写其他MemTable和新的Wal，wal没有上限，但是MemTable有上限。



WriteBatchInternal的带cfid的Put，只是将cfid放在key前面，默认是0；便于后面读出来进行MemTable插入。



SwitchMemTable只是创建一个新的MemTable，并不主从flush ，而是在最后`InstallSuperVersionAndScheduleWork`中，判断是否需要Flush(**仅仅是trigger**)；但是这里肯定会flush当前的wal，并创建一个新的wal。

```c++
// Attempt to switch to a new memtable and trigger flush of old.  
// Alway flush the buffer of the last log before switching to a new one
```





enable_pipelined_write和concurrent_prepare(two write queue)不兼容

而如果enable_pipelined_write打开，那么对于WAL和MemTable分别有一个队列，某write_thread提交的时候，先入wal队，后入MemTable队。这样wal的写盘只需要等待前一个group的WAL落盘，而不需要等待前一个group的WAL和MemTable都落盘。

> JoinBatchGroup：加入到某group中，如果不是leader就在此等待leader设置自己的状态。

1. write_thread_.JoinBatchGroup(&w); 将当前的w加入到组提交中，根据返回的状态分别处理自己的阶段的任务
   1. 如果返回的是STATE_PARALLEL_MEMTABLE_WRITER，写MemTable
   2. STATE_GROUP_LEADER，负责写该group的batch到WAL和MemTable，只不过会根据判断`write_thread_.LaunchParallelMemTableWriters` 唤醒其他write_thread写自己的MemTable。

disableWAL，基于另一个专门的group 队列，进行WAL的group write

nonmem_write_thread_.JoinBatchGroup



enable_pipelined_write

依然是group write，只不过分为两个阶段注册group。为保证MemTable的插入顺序和WAL一直，当某个wal group完成后，会将该group 的thread注册到当前的newest_memtable_writer_后

```cpp
    // Link the remaining of the group to memtable writer list.
    //
    // We have to link our group to memtable writer queue before wake up the
    // next leader or set newest_writer_ to null, otherwise the next leader
    // can run ahead of us and link to memtable writer queue before we do.
    if (write_group.size > 0) {
      if (LinkGroup(write_group, &newest_memtable_writer_)) {
        // The leader can now be different from current writer.
        SetState(write_group.leader, STATE_MEMTABLE_WRITER_LEADER);
      }
    }
```

当某writethread成为STATE_MEMTABLE_WRITER_LEADER后，会判断是否可以parallel，通过write_thread_.LaunchParallelMemTableWriters唤醒follower并行写自己的MemTable。



buildTable只是在FlushJob中用到，用来写Level0 sst



prefix extractor：在Option中提供一个方法，可以提取value的一部分数据，单独比较；这样可以做一些优化，比如：

- 如果定义了prefix extractor，那么seek的时候将只在prefix相关区间读取数据。如下代码片段，在迭代开始判断prefix和prefix_start_key是否相同，如果不同，直接退出。

- ```cpp
  
    do {
      if (!ParseKey(&ikey_)) {
        return false;
      }
    
      if (iterate_upper_bound_ != nullptr &&
          user_comparator_->Compare(ikey_.user_key, *iterate_upper_bound_) >= 0) {
        break;
      }
    
      if (prefix_extractor_ && prefix_check &&
          prefix_extractor_->Transform(ikey_.user_key)
                  .compare(prefix_start_key_) != 0) {
        break;
      }
  ```

- 

- 另外，在memtable中可以配置prefix extractor，如下，这样插入的时候可以根据prefix的hint，直接插入到指定位置，减少了比较。

- ```cpp
  
    // Extract sequential insert prefixes.
    const SliceTransform* insert_with_hint_prefix_extractor_;
    
    // Insert hints for each prefix.
    std::unordered_map<Slice, void*, SliceHasher> insert_hints_;
    ```


  ```

- 



​```cpp

uint64_t DBImpl::MinLogNumberToKeep() {
  if (allow_2pc()) {
    return versions_->min_log_number_to_keep_2pc();
  } else {
    return versions_->MinLogNumberWithUnflushedData();
  }
}
  ```

由上，log对应的memtable unflush，那么其log::writer 会一直保留在log_ 或 logs_to_free_中。





DBWriteImpl每次调用创建一个Writer，后续对这些Writer进行group 或pipeline。每个Writer负责将自己的的writebatch写到 wal和mem中。

1. 构造一个FlushJob，然后PickMemTablesToFlush：
   - cfd从 imm中找需要Flush的memtable，每个MemTable持有自己的edit，MemTable flush 为l0 file后，将l0fil meta加入到自己的edit中
2. FLushJob.run : WriteLevel0File
   1. BuildTable
   2. 将meta和blob meta加到edit中。
3. TryInstallMemtableFlushResults：
   - 将新的File Install到VersionSet中，一次FLush可能会刷多个MemTable，每个MemTable有一个自己的edit，因此这里VersionSet是将一个edit_list进行LogAndApply

LogAndApply中对于每个cfd，构造一个Manifest Writer；最终调用ProcessManifestWrites：

1. 判断是否有CF的变更，这和CF的atomic相关，需要做一些处理；最终准备好 builder_guards，可认为是version build的一些job，里面有需要的结构：
   - edit
   - version
   - versionbuilder
2. DoApplyAndSaveTo versionstorage
3. 写MANIFEST -log
4. 调callback





# SequenceNumber Assign





> ```
>   const bool use_batch_per_txn =
>       txn_db_options.write_policy == WRITE_COMMITTED ||
>       txn_db_options.write_policy == WRITE_PREPARED
> ```
>
> 这里假设`batch_per_trx=true`

RocksDB的Lsm-tree中的Internalkey都带有一个SequenceNumber，这个Seq是由VersionSet生成的。但是在VersionSet中，有三个seq

last_sequence <= last_published_sequence_ <=  last_allocated_sequence_

- **last_sequence**: 用户可见的Sequence

rocksdb的write会通过queue将writer进行排队，队列中的`writer->batch`会写到wal和MemTable（都是可选的），为了优化写入速度，又加了一个额外的queue（通过参数`two_write_queue`打开），这个queue只写WalOnly的batch，走`WriteImplWALOnly`逻辑。这里分别称这两个queue为：main queue(下称mq)/walonly queue(下称wq)。

> mq维护了last_sequence，wq维护了last_published_queue，

last_publish_queue只有在seq_per_batch=true，即使事务用WritePrepare的方式，并且打开two_write_queue时才有效，否则等于last_sequence

```
      // last_sequence_ is always maintained by the main queue that also writes
      // to the memtable. When two_write_queues_ is disabled last seq in
      // memtable is the same as last seq published to the readers. When it is
      // enabled but seq_per_batch_ is disabled, last seq in memtable still
      // indicates last published seq since wal-only writes that go to the 2nd
      // queue do not consume a sequence number. Otherwise writes performed by
      // the 2nd queue could change what is visible to the readers. In this
      // cases, last_seq_same_as_publish_seq_==false, the 2nd queue maintains a
      // separate variable to indicate the last published sequence.
      last_seq_same_as_publish_seq_(
          !(seq_per_batch && options.two_write_queues)),
```

WritePrepares Txn通过PrereleaseCallBack，在写完Wal后，更新last_published_queue(见WriteWalOnly)，

mq的逻辑是，先写wal，其中通过FetchAddLastAllocatedSequence递增`last_allocated_sequence_`，新的MemTable基于`last_allocated_sequence_+1`写mem（等于MemTable对应的batch持久化到日志中的Sequence，这个如果是WriteCommit的事务，这个Batch就是commit_time_batch，将prepare_batch append到waltermpoint之后得到的）。这样确保Batch与MemTable的Sequence能对上。

在MemTableInserter中，如果是默认的seq_per_key，那么每个key自行递增seq；而如果开启了seq_per_batch，那么基于batch_boundary进行seq递增（但是这里需要处理duplicate key的问题，这里引入了一个sub-patch的概念，表示WritBatch的一个没有重复key subset）

```
  // batch_cnt is expected to be non-zero in seq_per_batch mode and
  // indicates the number of sub-patches. A sub-patch is a subset of the write
  // batch that does not have duplicate keys.
```



l  // the individual batches. The approach is this: 1) Use the terminating
  // markers to indicate the boundary (kTypeEndPrepareXID, kTypeCommitXID,
  // kTypeRollbackXID) 2) Terminate a batch with kTypeNoop in the absence of a
  // natural boundary marker



```
  // batch_cnt is expected to be non-zero in seq_per_batch mode and indicates
  // the number of sub-patches. A sub-patch is a subset of the write batch that
  // does not have duplicate keys.
```



```cpp
// Note: the logic for advancing seq here must be consistent with the
// logic in WriteBatchInternal::InsertInto(write_group...) as well as
// with WriteBatchInternal::InsertInto(write_batch...) that is called on
// the merged batch during recovery from the WAL.
for (auto* writer : write_group) {
  if (writer->CallbackFailed()) {
    continue;
  }
  writer->sequence = next_sequence;
  if (seq_per_batch_) {
    assert(writer->batch_cnt);
    next_sequence += writer->batch_cnt;
  } else if (writer->ShouldWriteToMemtable()) {
    next_sequence += WriteBatchInternal::Count(writer->batch);
  }
}
```





















































writebatch中的jkey value都是lengthprefix的





SwitchMemTable中，只要当前log不空，那么就需要刷最后一个log，切换新的log

```cpp
bool creating_new_log = !log_empty_;
```



Memtable 之后的value就不带前面的prefix length了



rebuilding_trx_是为了支持recovery的时候，WAL中有prepare的恢复[commitlog](https://github.com/facebook/rocksdb/commit/1b8a2e8fdd1db0dac3cb50228065f8e7e43095f0)





[cached_recoverable_state_](https://github.com/facebook/rocksdb/commit/17731a43a6e6a212097c1d83392f81d310ffe2fa) 在2pc中，记一些专门在recover需要的信息，比如在MyRocks中。。。



LogAndApply

1. 基于传入的cfd和edit，构造ManifestWriter

1. VersionSet::ProcessManifestWrite，传入ManifestWriter，逐个执行

1. 1. 基于一堆ManifestWriter的cfd，构造一批BaseReferencedVersionBuilder（取出cfd的current，并引用，析构时释放）

1. 1. 逐个调用BaseReferencedVersionBuilder::DoApplyAndSaveTo

1. 1. 1. N次 version_builder->Apply

1. 1. 1. 1次 version_builder->SaveTo





1035

mysql bugs: https://github.com/mysql/mysql-server/commit/cbd87693248cb4d2a217a8f1c13108e3eca1ecff

如果称聚集索引的事务信息是行级的话（每一行都保存事务和回滚信息），那么，二级索引的事务信息就是页级的。数据页头中的PAGE_MAX_TRX_ID就是这个页级的事务ID，并且只有二级索引页有效。

每当事务修改某条二级索引记录时，都会修改当前页面的PAGE_MAX_TRX_ID的值。当一个读事务某二级索引记录时，InnoDB根据一定的算法，使用PAGE_MAX_TRX_ID和当前读事务ID来判断该记录是否不可见。如果不可见，再定位对应聚集索引来判断真实的可见性。



**索引键**：B+树的排序键，对于聚集索引就是主键，对于二级索引是二级索引键+主键



当我们有两个写请求队列（two_write_queues=true），那么主写队列可以同时向WAL和memtable写，而第二写队列只能写WAL，这个会被用于在WritePrepared事务中写提交标记。在这个场景下，主队列（以及他的PreReleaseCallback回调）总是用于准备项，而第二队列（以及他的PreReleaseCallback回调）总是用于提交。这样 i)避免两个队列的竞争 ii)维护了PreparedHeap的顺序增加，以及 iii)简化代码，避免并发插入到CommitCache（以及从中淘汰数据的代码）。