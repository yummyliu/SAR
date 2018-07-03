---
layout: post
title: 
date: 2018-07-03 12:20
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---





## MultipleTable Plan

#### 预处理

1. 计划与转化CTE
2. 如果from中的子查询没有group by，having order by 等操作，上拉子查询
3. 可能的话，将外链接转化成内连接

#### 得到代价最小的路径

多个表的查询计划的获得是一个昂贵的操作，辛亏在表数目小于12的时候可以使用动态规划的方式来得到最优plan，当大于这个数目是使用遗传算法。

##### 基于动态规划得到最优路径

1. 得到每个table的最优path；
2. 得到每两个table的最优path；
3. 基于2中的结果，得到每三个表的最优path
4. 同上直到结束

![multitable](/image/fig-3-31.png)

## Join

PostgreSQL 中支持三种Join算法和所有的Join操作；

Join算法：

1. nested loop join
2. merge join
3. hash join

Join操作：

1. INNER JOIN
2. LEFT/RIGHT OUTER JOIN
3. FULL OUTER JOIN

这里主要讨论的是NATURAL INNER JOIN；

#### Nested Loop Join

这是最基础的join，可以在任何条件下使用。PostgreSQL中支持原生的还有5中变种。

###### Nested Loop Join

内表外表的内外是循环的内外，在循环外层的叫外表，在循环内层的叫内表（也可以这么理解，join都是以某一个tuple去找另一个匹配的tuple，被寻找的就叫innnertable）。逐行扫描外表，search内表找到匹配join条件的tuple，所以内表一般是有索引的大表。

start-up cost=0

run cost=(cpu_operator_cost+cpu_tuple_cost)×Nouter×Ninner+Cinner×Nouter+Couter

Cinner和Couter是内外表扫描的代价，内表需要扫描Nouter次，所以代价如上。

###### Materialized Nested Loop Join

> PostgreSQL内部提供了一个`temporary tuple storage（TTS）`的模块（tuplestore.c）,用来materializing table,或者在hashjoin的时候createbatches；可能使用work_mem或者temporary file，取决于tuple的数量；

在进行join之前，将innertable的元组读取到TTS中，这比起通过buffer manager扫描快，至少如果全部用到了work_mem的时候快；

###### Indexed Nested Loop Join

如果innertable上有join列上的索引，PostgreSQL会使用innertable上的索引；

###### 其他变种

另外就是如果outertable上也有索引，或者where条件中可以减少outer表的数量，这种信息也能用上

![out](/image/fig-3-19.png)

#### Merge Join

###### Merge Join

![lll](/image/fig-3-20.png)

先在work_mem或者temp file中排序后，然后merge

###### Materialized Merge Join

和Nested Loop Join类似，同样可以将innertable排序之后materialize一下，inner表的查找速度；

###### 其他变种

同样类似Nest Loop，对外表的扫描如果有索引列，就可以不用sort；

![](/image/fig-3-22.png)



#### Hash Join

和merge join相似，hash join只能用于自然连接和等值连接。

基于表大小的不同，hash join的方式可能不同；如果innertable比较小（<=work_mem的25%），会使用two-phase in-memory join; 否则使用，hybrid hash join; 如果建立hashtable的时候没有任何冲突，start-up和run的代价估计是O(Ninner+Nouter)；

###### In-Memory Hash Join

1. inner计算hash函数，建立hashtable
2. outer计算hash函数，probe hashtable

![](/image/fig-3-23.png)

![](/image/fig-3-24.png)



###### Hybrid Hash Join

​	当innertable不能放到work_mem中，需要将innertable分成若干batch装载进work_mem中，一个batch一个batch的处理。按照hash column的hashkey的后n位，分成2^n个batch，每个batch中有2^m个bucket。这样基于hashkey的末尾(n+m)位，可以定位该tuple位于那个batch的哪个bucket中。

​	![](/image/fig-3-25.png)

​	通过使用上文提到的PostgreSQL中TTS机制（综合利用work_mem和tempfile），建立初步的hashtable。由于inner 和outer都需要分批次处理，这样build-probe这个过程需要执行2^n次。第一个批次的时候，所有的batch都被创建了，并且inner和outer的第一个batch都被处理了。这样后面的几批次都需要在tempfile中操作，这很耗时。PostgreSQL在基本batch上，额外提供了一个特殊的batch，即**skew**，在第一个批次的时候尽可能的处理更多的tuples，大概的意思就让第一批次的hashkey对应到inner表的join条件列，在outer中出现频次高的那些值上，这样在第一批次处理的时候，outer越不均匀，外表被处理的tuple越多，而第一批次都是在work_mem，probe效率更高。

![](/image/fig-3-26.png)

![](/image/fig-3-27.png)

![](/image/fig-3-28.png)

![](/image/fig-3-29.png)

在inner的build阶段，除了按照常规建立batch_0~batch_2^n之外，会按照某个方法判断这个tuple是不是outertable的MCV（频次高的值），是的话插入到特定的skew batch中。在outer的第一次probe过程中，判断如果是MCV，那么与skew batch中的tuple进行join，如图(6)箭头，如果要么和内存中batch_0按照常规join，要么放在outer表自己在tempfile中的batch_1…2..3_out文件中，等后续操作。第一轮结束后outertable的MCV tuple 和本来属于batch_0都已join好了（8）。

接下来清理work_mem中的skew batch和batch_0，将后面batch中的tuple处理了。

###### Join AccessPath & Join Node

介绍完详细的算法，每个算法就是plan数的一个执行节点，该node提供执行时需要的信息。如下

![](/image/fig-3-30.png)

[interdb-3](http://www.interdb.jp/pg/pgsql03.html)