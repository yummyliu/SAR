---
layout: post
title: RocksDB的Merge
date: 2020-06-19 14:25
categories:
  - rocksdb
typora-root-url: ../../layamon.github.io
---
> * TOC
{:toc}

回收旧版本数据，需要注意是否有快照还在使用。对于某个Key有如下快照：

```
K:   OP1     OP2     OP3     OP4     OP5  ... OPn
              ^               ^                ^
              |               |                |
           snapshot1       snapshot2       snapshot3
```

如果没有Merge类型，那么对于每个snapshot只需要保留最近的一个OP数据；有了Merge后，Compact和Get类似都是从new->old扫描数据，将扫过的merge操作暂存，直到可以执行FullMerge；

但Compact和Get不同，Compact是针对某一个sst file操作，Get可以全局扫描；Compact如果遇到end-of-file还没结束，那么这次就不能执行FullMerge；

另外，如果碰到有Supporting Operation的SnapShot，Supporting Operation暂时不可删除，那么清理到当前的栈后，可以从Supporting Operation开始继续compact。

## Merge

> Merge可看作是一个升级版的Put，Put直接给出key的新值，Merge会根据当前Val得到新值。

为了减少read-modify-write时的额外IO代价，引入了Merge Operator。这是用户自定义的回调操作，用户将**增量更新**语义抽象在Merge操作中；

Merge操作的具体执行时机可能在Get的时候，也可能在Compact清理的时候；这时Merge的操作对象可能已经累积不少了。要计算到Merge记录的val，需要向前追溯，直到找到该key的一个Put或者Delete记录；将Put的val和merge的val进行合并，最后得到正确的值；（这叫FullMerge）

但有可能merge的链很长，这个操作就很耗时；为避免这个问题，用户的使用场景中，如果Merge语义能够级联（即，Merge的输出可以作为另一个Merge的输入），那么可以定义`PartialMerge`接口，这样可以提前对多个Merge操作进行部分合并成一个merge；减少了最后`FullMerge`时的merge量。将IO代价进行均摊，这样得到一个线性扩展的性能曲线。

> `PartialMerge`
>
> it should be known that PartialMerge() is an optional function, used to combine two merge operations (operands) into a single operand. For example, combining OP(k-1) with OPk to produce some OP', which is also a merge-operation type. 

用户一般继承子类`AssociativeMergeOperator实现自己的逻辑，`AssociativeMergeOperator`将`FullMerge`和`PartialMere`合并为一个Merge，比如下这些Operator都有增量更新的特点。

```cpp
storage/rocksdb/terarkdb/utilities/merge_operators/bytesxor.h <<GLOBAL>>
             class BytesXOROperator : public AssociativeMergeOperator {
storage/rocksdb/terarkdb/utilities/merge_operators/string_append/stringappend.h <<GLOBAL>>
             class StringAppendOperator : public AssociativeMergeOperator {
storage/rocksdb/terarkdb/utilities/merge_operators/uint64add.cc <<GLOBAL>>
             class UInt64AddOperator : public AssociativeMergeOperator {
```

## 