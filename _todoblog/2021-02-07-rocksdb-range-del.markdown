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
使用RocksDB的系统，往往相关的key使用一个共同的前缀，常常存在场景删除这一批key，比如MyRocks的IndexID等；在没有DeleteRange的时候，可以使用CompationFilter进行异步删除，但是用户想立马将这些数据不可见，就需要真正把数据删除，而一个个删除代价太大，基于此需要，RocksDB提供了DeleteRange操作。

# RangeDeletion

- 写：

DeleteRange是和Put/Delete平级API，对应的Type是`kTypeRangeDeletion`。写入时，MemTable维护两个具体实现：table和range_del_table，[kTypeRangeDeletion类型的数据写到专门的range_del_table中](https://github.com/facebook/rocksdb/blob/6a85aea5b1f62a447b2e413ee9c49be04c36a4d8/db/memtable.cc#L550)，最后Flush的时候作为meta block写到同一个sst中。

- 点查：

在Get流程中，[只维护key的最大tombstone seq](https://github.com/facebook/rocksdb/commit/8c78348c77940d8441d51bf2558bd9bd36c37f07)即可，最终在GetContext的SaveValue中[判断max_covering_tombstone_seq与key.Sequence的关系](https://github.com/facebook/rocksdb/blob/8c78348c77940d8441d51bf2558bd9bd36c37f07/table/get_context.cc#L189)，得知该Key是否被DeleteRange删除，这样免去了Get中构建TombStone Skyline的代价。

- 扫描：

RangeScan会涉及多个有序对象（包括Mem/imm和Sst），每个对象都有对应的RangeDelete TombStone；在扫描过程中，需要知道每个Key是否ShouldDelete；需要用额外的结构，存储并查询涉及的有序对象的TombStone，这就是RangeDeletionAggregator。

调用AddTombstones构建RangeDeletionAggregator，通过ShouldDelete判断Key是否为`kTypeRangeDeletion`。

这里扫描有两类，一个是用户基于DBIter的扫描，其在FindNextUserEntryInternal判断。另外在Compaction.NextFromInput/MergeUntil内判断。

- TombStone的存储与转移

Mem中使用单独的MemTable存储RangeDeletion信息，并且没有Fregment；

在Flush的时候，将MemTable中的range_del_table持久化为MetaBlock，这里并不需要将tombstone排序，留待读SST的时候排序且进行Fragment，并缓存在TableReader中。生成的L0-SST中，如果存在TombStone，该SST的boundary会考虑上TombStone的start&end（注意：SST中的boundary以internalkey作为边界，TombStone的start&end都是UserKey，那么作为SST边界时 start.seq = tombstone.seq，end.seq = kMaxSequenceNumber）。

在后续Compaction时，将Input中的TombStone转移到Output中，但是会判断SnapShot、边界等条件，确保[只转移有效的tombstone](https://github.com/facebook/rocksdb/blob/8c78348c77940d8441d51bf2558bd9bd36c37f07/db/compaction_job.cc#L1212)；为了确保SST之间不存在overlap，对于TombStone会按照文件实际的最大最小是进行Boundary Truncate（Internal Key Range TombStone Boundary），并且这样Pick的时候，确保每个Compact的Input都是不相交的，对于存在overlap的SST在一个Compact中执行（Atomic Compact Unit）。

当TombStone落到最低层，即可删除了。

## Fragmented RangeTombstone

MemTable和Sst中的TombStone都是没有Fragment的，在查询的时候需要遍历全部集合；为了提高Key查询TombStone的效率，有了FragmentRangeTombStone，这样对于每个Key，可以通过binary search找到对应的fragment TombStone。

```
FragmentTombstones(unfrag_ranges):
	assert(issort(unfrag_ranges))
	cur_start_key = (nullptr,0)
	cur_end_keys = {} // order set
	for range in unfrag_ranges:
		
		if(range.start != cur_start_key) {
		next_start_key = range.start
			flush_range_between_curstart_and_next_start(cur_start_key, range.start);	
		}
		
		cur_start_key = range.start
		cur_end_keys.emplace(range.end, range.seqno, kTypeRangeDelete)		
```

最终得到每个Sst的FragmentedRangeTombstoneList

```
​```
tombstones_:     [a, b) [c, e) [h, k)
                   | \   /  \   /  |
                   |  \ /    \ /   |
                   v   v      v    v
tombstone_seqs_: [ 5 3 10 7 2 8 6  ]
​```
```

## RangeDelAggregator

扫描场景（Compact、MergeOperator）中，通过构建一个MergeIterator，在多个Input之上迭代，那么对于每个Key，此时并不知道对于哪个有序对象，无法得知FragmentRangeTombStone，此时在全局维护一个RangeDelAggregator。

当存在多个SnapShot，根据TombStone对于每个SnapShot的可见性，构建每个SnapShot的Aggregator，因此RangeDelAggregator是一个两层索引：

**SnapShotStripe -> beginKey -> TombStone**

Links

[DeleteRangeImpl](https://github.com/facebook/rocksdb/wiki/DeleteRange-Implementation)

