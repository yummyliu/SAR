---
layout: post
title: 论文精读——基于硬件进行优化的多核HashJoin算法
date: 2015-02-28 10:23
header-img: "img/head.jpg"
categories: jekyll update
tags:
 - Database
typora-root-url: ../../yummyliu.github.io
---

* TOC
{:toc}

> Main-Memory Hash Joins on Multi-Core CPUs: Tuning to the Underlying Hardware

对于多核环境下的Join算法的新的设计思路，有两种方向：一种是觉得现在的硬件已经能够屏蔽底层的cache miss、TLB miss或者内存带宽的影响。另一种还是倾向于设计适应硬件的算法。本文中就Hash join算法进行讨论，比较其考虑硬件与不考虑硬件时的处理性能的差异。

# 硬件环境

## 并行层次

指令并行（super scalar execution）：超流水线；CPU指令可能有若干个阶段，比如：获取指令，解码指令，执行，内存访问，寄存器回写等等；每个阶段操作的对象不同，在一个CPU的时钟周期中，就可以对不同指令的不同执行阶段进行同时执行。这就是指令级别的并行。
数据并行（SIMD）： 单指令多数据；一个指令同时处理多个数据。
线程并行：线程并行有两种，主要区别就是在一个指令周期里CPU上执行的指令是来自一个线程还是多个线程；来自一个线程叫Temporal multithreading；在超流水线中执行的多个指令可能来自多个线程叫Simultaneous multithreading。

## 存储层次

按照距离CPU的远近，有cache size、TLB size、page size和table size。另外page在内存中的结构，以及table在磁盘中的组织形式也是会对整体性能有所影响。

这样我们再计算的时候，计算数据单元的大小应该和这些存储层次适配，才能发挥最佳性能。

# Hash Join

## 硬件无关的Join

### Canonical Hash Join

通常来说，R与S两张表进行**HashJoin**分为两步：

1. Build：对于R，建立一个HashTable
2. Probe：遍历S，计算Tuple的Hash值，然后从R的HashTable进行匹配。

这就是**传统HashJoin**，这和底层硬件没有任何适配。HashTable 查找复杂度是O(1)，每个关系表扫描一次，那么整个算法的复制度为O(|R|+|S|)。

### Parallel No-Partition Hash Join

在传统HashJoin上，我们可以在Build阶段和Probe加上并行；

1. Build：将R分成等长的若干块，然后将这些数据库交给N个线程分别计算Hash，然后写入到线程共享的HashTable中。
2. 等待所有线程结束后（thread barrier），然后N个线程同时对S进行Hash计算，然后再共享的HashTable中找匹配。

在这个并行算法中，每个HashTable都是共享么，那么线程之间就需要同步。每个线程想要修改HashTuple，需要获取该Tuple上的Latch。但是由于**HashTable可能比较大**，并且在Probe阶段都是**只读**的，其实锁竞争代价还是比较小的。那么对于一个有N核的系统来说，算法复杂度就是O((|R|+|S|)/N)，这个算法叫**NoPartitioning Join**。

## 硬件相关的Join

### Partition HashJoin

上述算法的基本点就是先生成一个较大的HashTable，那么在内存中对HashTable的随机访问就可能造成较多的CacheMiss。因此，为减少CacheMiss，提出了将HashTable切分为若干个CacheSize大小的块，至此原HashJoin就加了一部Partition步骤，这个算法可称为**Partition HashJoin**，分为三步。

1. Partition：按照HashFunc1，将R和S分别划分为若干个ri和si（由于基于HashFunc1进行分区，这样ri和sj之间不会有交集）。
2. Build：对于每个ri，按照HashFunc2，建立ri的的哈希表hi（这里是使用第二个Hash函数，创建Hash表）。
3. Probe：对于每个si，按照HashFunc2，在响应的hi中找匹配。

这个算法在Build和Probe阶段使用了和Cache大小相同的HashTable，那么可以减少CacheMiss。但是引入的Partition阶段，会将各个Partition放在不同的内存页上；在虚拟内存映射表中对于每一页都需要一个条目；虚拟内存映射表的缓存叫TLB；因此，如果创建了很多Partition的话，TLB就会溢出，导致最终的TLB MISS；

因此，可用的TLB条目大小决定了**可以高效使用的分区数**的上限，研究学者进一步考虑了Partition阶段的TLB的影响，最终提出了RadixJoin算法。

### Radix Join

#### Radix Partition

Radix这里可以理解为JoinKey的若干个bit位。在Partition阶段，将分多步基于Radix将R和S进行分块，就能得到CacheSize大小且不会导致TLBMiss的hashTable，其中关键的是：什么是Radix Partition？其可并行执行，有三步：

1. 统计直方图：每个Radix取值下的元组数。
2. 计算每个Radix的Prefix Sum。
3. 基于PrefixSum，对原tuple的位置重新调整。进而就将表进行的分区。

基于Radix进行分区后，整个Partition还是在一整块内存区域，避免了零散的内存Page导致的TLB过大。

最后，整个算法还是分为三步：

1. 对R和S进行两三次的Radix Partition；
2. Build：对于每个ri，按照HashFunc2，建立ri的的哈希表hi（这里是使用第二个Hash函数，创建Hash表）。
3. Probe：对于每个si，按照HashFunc2，在响应的hi中找匹配。













