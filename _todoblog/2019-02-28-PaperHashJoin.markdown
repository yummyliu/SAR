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

## 并行计算（parallelism）

指令并行（super scalar execution）：超流水线；CPU指令可能有若干个阶段，比如：获取指令，解码指令，执行，内存访问，寄存器回写等等；每个阶段操作的对象不同，在一个CPU的时钟周期中，就可以对不同指令的不同执行阶段进行同时执行。这就是指令级别的并行。
数据并行（SIMD）： 单指令多数据；一个指令同时处理多个数据。
线程并行：线程并行有两种，主要区别就是在一个指令周期里CPU上执行的指令是来自一个线程还是多个线程；来自一个线程叫Temporal multithreading；在超流水线中执行的多个指令可能来自多个线程叫Simultaneous multithreading。

## 存储层次

按照距离CPU的远近，有cache size、TLB size、page size和table size。另外page在内存中的结构，以及table在磁盘中的组织形式也是会对整体性能有所影响。

这样我们再计算的时候，计算数据单元的大小应该和这些存储层次适配，才能发挥最佳性能。

# Hash Join

通常来说，R与S两张表进行HashJoin分为两步：

1. Build：对于R，建立一个HashTable
2. Probe：遍历S，计算Tuple的Hash值，然后从R的HashTable进行匹配。

通常来
