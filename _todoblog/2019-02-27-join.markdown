---
layout: post
title: JOIN算法综述
date: 2015-02-27 11:24
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - Database
typora-root-url: ../../yummyliu.github.io
---
* TOC
{:toc}
# Nest Loop Join
最简单直观的JOIN算法，基本所有的数据中都会支持，基本逻辑就是双层循环：
```
for a in A:
	for b in B:
		if a == b:
			append one result
```
在Nest Loop中，我们一般我们将小表作为A，也叫外表或者驱动表；内表较大，且一般有索引。在MySQL只有这一类JOIN算法。

# (Radix-)Hash Join

在HashJoin中，主要有三步：
+ Partition：基于JoinKey的前N个bit位的Hash函数,将A/B分成若干桶。
+ Build：基于JoinKey，建立A的HashTable。
+ Probe：遍历B中的每个元组，基于A的HashTable进行Match。

# Sort Merge Join



