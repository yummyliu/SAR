---
layout: post
title: RocksDB-- RangeDelete
date: 2021-02-07 16:47
categories:
  - RocksDB
typora-root-url: ../../layamon.github.io
---
* TOC
{:toc}
使用RocksDB的系统，往往相关的key使用一个共同的前缀，常常存在场景删除这一批key，比如MyRocks的IndexID等；在没有DeleteRange的时候，可以使用CompationFilter进行异步删除，但是用户想立马将这些数据不可见，就需要真正把数据删除，而一个个删除代价太大，DeleteRange就是为此优化的。

# RangeDeletion

- 写：

DeleteRange是和Put/Delete平级API，对应的Type是`kTypeRangeDeletion`。写入时，MemTable维护两个具体实现：table和range_del_table，[kTypeRangeDeletion类型的数据写到专门的range_del_table中](https://github.com/facebook/rocksdb/blob/6a85aea5b1f62a447b2e413ee9c49be04c36a4d8/db/memtable.cc#L550)，最后Flush的时候作为meta block写到同一个sst中。

- 点查：

在Get流程中，[只维护key的最大tombstone seq](https://github.com/facebook/rocksdb/commit/8c78348c77940d8441d51bf2558bd9bd36c37f07)即可，最终在GetContext的SaveValue中[判断max_covering_tombstone_seq与key.Sequence的关系](https://github.com/facebook/rocksdb/blob/8c78348c77940d8441d51bf2558bd9bd36c37f07/table/get_context.cc#L189)，得知该Key是否被DeleteRange删除，这样免去了Get中构建TombStone Skyline的代价。

- 扫描：

每个有序对象（包括Mem/imm和Sst）都有对应的RangeDelete TombStone，调用AddTombstones构建RangeDeletionAggregator，通过ShouldDelete判断Key是否为`kTypeRangeDeletion`。

这里扫描有两类，一个是用户基于DBIter的扫描，其在FindNextUserEntryInternal判断。另外在Compaction.NextFromInput/MergeUntil内判断。

- TombStone的转移与消亡

上面讲在Flush的时候将MemTable中的range_del_table持久化为MetaBlock，而在每次Compaction时同样将Input中的TombStone转移到Output中，但是会判断SnapShot、边界等条件，确保[只转移有效的tombstone](https://github.com/facebook/rocksdb/blob/8c78348c77940d8441d51bf2558bd9bd36c37f07/db/compaction_job.cc#L1212)；这里并不需要将tombstone排序，留待读的时候构建Skyline。

