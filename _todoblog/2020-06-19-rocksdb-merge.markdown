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
# Merge Overview

为了减少read-modify-write时的额外IO代价，引入了Merge Operator。Merge可看作是一个升级版的Put，Put直接给出key的新值，Merge会根据当前Val得到新值。这是用户自定义的回调操作，用户将**增量更新**语义抽象在Merge中；

实现Merge操作，需要用户定义自己的MergeOperator，MergeOperator提供了两个接口：`FullMergeV2`和`PartialMerge`；这样RocksDB在Get/Iter的过程中，在**合适的时机**执行**合适的Merge操作**。

- Merge In Get/Iter：执行FullMerge

- Merge In Compact：视情况而定，可能是FullMerge也可能是PartialMerge，或者什么也不做。

>  Partial Merge: 如果Merge语义能够级联（即，Merge的输出可以作为另一个Merge的输入），那么可以定义`PartialMerge`接口；
>
> Compact回收旧版本数据，需要注意是否有快照还在使用。Compact和Get不同，Compact是针对某一个sst file操作，Get可以全局扫描；Compact如果遇到end-of-file还没结束，那么这次就不能执行FullMerge，但是如果MergeOperator支持PartialMerge，这样可以提前对多个Merge操作进行部分合并成一个merge；减少了最后`FullMerge`时的merge量。将IO代价进行均摊，这样得到一个线性扩展的性能曲线。

# Merge Implement

- MergeOperator：
- MergeContext：Get和Iter会维护一个merge_context_；这样在具体的
- MergeHelper：

