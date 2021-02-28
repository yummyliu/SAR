---
layout: post
title: RocksDB的Merge
date: 2020-06-19 14:25
categories:
  - rocksdb
typora-root-url: ../../layamon.github.io
---
* TOC
{:toc}
为了减少read-modify-write时的额外IO代价，引入了Merge Operator。Merge可看作是一个升级版的Put，Put直接给出key的新值，Merge会根据当前Val得到新值。这是用户自定义的回调操作，用户将**增量更新**语义抽象在Merge中；

- 时机

Merge操作的具体执行时机可能在Get的时候，也可能在Compact清理的时候；Get的时候要执行FullMerge，Compact的时候视情况而定，可能是FullMerge也可能是PartialMerge，或者什么也不做。

- PartialMerge

如果Merge语义能够级联（即，Merge的输出可以作为另一个Merge的输入），那么可以定义`PartialMerge`接口，这样可以提前对多个Merge操作进行部分合并成一个merge；减少了最后`FullMerge`时的merge量。将IO代价进行均摊，这样得到一个线性扩展的性能曲线。

主要用在Compaction过程中，此时对于存在Snapshot的情况；

- 数据回收

Compact回收旧版本数据，需要注意是否有快照还在使用。Compact和Get不同，Compact是针对某一个sst file操作，Get可以全局扫描；Compact如果遇到end-of-file还没结束，那么这次就不能执行FullMerge；

- 使用方式

用户一般继承子类`AssociativeMergeOperator实现自己的逻辑，`AssociativeMergeOperator`将`FullMerge`和`PartialMere`合并为一个Merge，比如下这些Operator都有增量更新的特点。

```cpp
storage/rocksdb/terarkdb/utilities/merge_operators/bytesxor.h <<GLOBAL>>
             class BytesXOROperator : public AssociativeMergeOperator {
storage/rocksdb/terarkdb/utilities/merge_operators/string_append/stringappend.h <<GLOBAL>>
             class StringAppendOperator : public AssociativeMergeOperator {
storage/rocksdb/terarkdb/utilities/merge_operators/uint64add.cc <<GLOBAL>>
             class UInt64AddOperator : public AssociativeMergeOperator {
```

