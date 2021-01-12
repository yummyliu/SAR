---
layout: post
title: WriteUnprepared
date: 2020-11-08 13:01
categories:
  - RocksDB
typora-root-url: ../../layamon.github.io
---
* TOC
{:toc}
RocksDB通过SequenceNumber向上返回某个Key最新的数据，而RocksDB的TransactionDB同样基于SequenceNumber来做事务隔离。默认地，当WriteBatch的数据，写入Wal后（即Commit），才写入memtable，这样LSM-tree中都是已提交的数据。那么，取决于何时获取SnapShot，决定隔离级别如何。

如上流程，而我们发现，WAL的写入必须在MemTable之前，那么事务的提交路径就会成串行的，如下：

1. 写WriteBatch
2. WriteBatch执行Prepare
3. WriteBatch执行Commit

WAL是必须写入的，那么写MemTable能否并行起来？而在Prepare阶段写日志，LSM-tree中的key就会和prepare_seq绑定，而不是commit_seq。那么如何判断一个key的prepare_seq是否已经commited呢？这里引入了额外的结构用来记录窗口信息：最老的prepare_seq和最新的commit_seq。


>  [**Write Prepared**](http://liuyangming.tech/05-2020/write-prepared.html)
>
>  <img src="/image/write-prepared/structures.png" alt="image-20200711171439816" style="zoom: 50%;" />
>
>  ```c++
>    const bool use_seq_per_batch =
>        txn_db_options.write_policy == WRITE_PREPARED ||
>        txn_db_options.write_policy == WRITE_UNPREPARED;
>    const bool use_batch_per_txn =
>        txn_db_options.write_policy == WRITE_COMMITTED ||
>        txn_db_options.write_policy == WRITE_PREPARED
>  ```
>
>  在RocksDB中，对于2PC的事务，会有两个batch，那么就有两个SeqNo（seq_per_batch）；WritePrepare的方式在Prepare阶段就开始写MemTable，那么version中的key的seq就是prepare_seq。i
>
>  因此，提供了一些额外的结构维护prepare_seq->commit_seq的映射关系。为提高性能，这里采用了Cache的优化策略（CommitCache）定期（max_evicted_seq）驱逐旧的映射关系到old_commit_map中；而PrepareHeap就是维护了最老的prepare_seq。

# Write Unprepared

概述

- ReadCallback
  - 对已经写入到Version内的可以，进行snapshot check
- RollBack Algo
  - 将事务内修改的key的旧值与trx绑定，trx取消更方便
  - 以提交的方式回滚事务，可重用现有的live snapshot的检查

- TransactionLockMgr
  - 自动将集中的key lock，升级为range lock。

-------------

操作

- Put：Transaction负责维护写到version中的unprepared_batch，并且也有一个类似prepare_head的unprepared_heap。

- Prepare：[BeginUnprepareXID]...[EndPrepare(XID)] ... [BeginUnprepareXID]...[EndPrepare(XID)] ... [**BeginPersistedPrepareXID**]...[EndPrepare(XID)] ... ...[**Commit**(XID)]

  WriteUnprepared可以从WritePrepare生成的WAL的结构恢复，反之则否。

- Commit：为了可见性判断，这里需要维护多个unprepare_seq->commit_seq的映射。同样cache驱逐的时候，需要全部删除
- RollBack：