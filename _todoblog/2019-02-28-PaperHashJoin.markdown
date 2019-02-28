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

对于多核环境下的Join算法的新的设计思路，有两种方向：一种是觉得现在的硬件已经能够屏蔽底层的cache miss、TLB miss或者内存带宽的影响。
另一种还是倾向于设计适应硬件的算法。本文中就Radix-Hash join算法进行设计比较其处理性能的差异。

# 硬件环境

## parallelism

指令并行（super scalar execution）：超流水线；对于CPU指令可能有若干个阶段，比如，获取执行，解码指令，执行，内存访问，寄存器回写等等；
每个阶段操作的对象不同，在一个CPU的时钟周期中，就可以对不同指令的不同执行阶段进行同时执行。
数据并行（SIMD）： 单指令多数据；一个指令同时处理多个数据。
线程并行（Temporal multithreading）：线程并行有两种，主要区别就是在一个指令周期里CPU上执行的指令是来自一个线程还是多个线程；
线程并行（Simultaneous multithreading）：在超流水线中执行的多个指令可能来自多个线程。

## 各种Size

page size、TLB size、cache size和table size以及表的组织形式。

# Hash Join

通常来说的HashJoin分为两步：

1. build：对于其中一个表建立一个HashTable
2. probe：遍历另一个表，计算Hash值，然后从HashTable找匹配。

通常来
