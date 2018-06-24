---
layout: post
title: cstore与Postgresql的fdw
date: 2016-06-02 14:32
header-img: "img/head.jpg"
tags:
    - CitusDB
    - PostgreSQL
---

#### 概述

创建PG的插件的方式分为两种，一种利用hook，一种编写C函数然后和数据库关联。而FDW插件属于后一种。
实现FDW需要实现两个C函数，Handler和 Validator （可选）。然后写一个control文件，control文件是在create extension加载的文件，根据这个文件，PG去寻找对应的sql文件，在 `create extension … ` 的时候调用，执行相应的sql文件来建立和数据库的关联。比如cstore_fdw.sql文件内容：
![cstorefdwdql](/image/cstorefdwsql.png)
 	完成fdw的开发工作，主要的工作就是实现两个C函数，就是用户自定义函数的编写，而其中Handler函数的返回是一个结构体，其中包含很多函数指针，如下：
![fdwroutine](/image/fdwroutine.png)

#### 详述

对于外表的scan是必须要实现的方法，同时也支持了对外表的其他操作，择需而定。

fdw的回调函数GetForeignRelSize, GetForeignPaths, GetForeignPlan, 和 PlanForeignModify 必须和pg的planner运行机制相符。
以下就是他们要做到的东西

> note：root和baserel的信息能够减少必须要从外部表获得的信息数量，从而降低消耗。
> baserel 是 RelOptInfo 类型
> baserel->baserestrictinfo 是一个链表。其中都是Restrictinfo 类型，
> Restrictinfo类型 在pg的relation.h文件中。和sql中的WHERE和join/on相关。相当于AND类型，所以使用这个信息来过滤元素，达到优化。
> 如果对于一个基本表，那么会出现在基本表的baserestrictinfo中
> 多个表就是在joininfo中，总之就是用来过滤的。

##### 关键函数
可参考[funcs][funcs]
###### GetForeignRelSize
```
void GetForeignRelSize (PlannerInfo *root,
                   		RelOptInfo *baserel,
                   		Oid foreigntableid);
```
root：查询的全局信息
baserel：查询关于这个表的信息
foreigntableid：pg_class中关于这个外部表的Oid
该函数在指定外部表扫描计划的时候调用，利用baserel->baserestrictinfo来估算好表返回的元组数，更新baserel-rows的值

###### GetForeignPaths
```
void GetForeignPaths (	PlannerInfo *root,
						RelOptInfo *baserel,
						Oid foreigntableid);
```
生成外部表扫描的路径，可能有多条路径，每条路径要有相应的cost。调用create_foreign_path来生成一个ForeignPath结构，然后add_path添加到baserel->pathlist中
ForeignPath结构如下:
![fdwpath](/image/fdwpath.png)
Cstoregetforeignpath

从进程的Recache中取到foreigntableid，对应的RelationData，double check

取到查询需要的列的列表，querycolimnlist
取到该表的数据文件的块大小 relationPageCount
取到该表的关系的属性总数，relationColumnCount

由上述信息得到查询列/总列数的比值 querycolumnratio，由此估算查询的页数，从而计算磁盘数据读取代价

最终总代价由，startucost+totalcpucost+totaldiskaccessCost
最后由上述得到的信息构造一个Path，ForeignPath

###### GetForeignPlan
```
ForeignScan * GetForeignPlan (	PlannerInfo *root,
								RelOptInfo *baserel,
								Oid foreigntableid,
								ForeignPath *best_path,
								List *tlist,
								List *scan_clauses,
								Plan *outer_plan);
```
基于之前得到的path，得到ForeignScan计划，推荐函数中使用make_foreignscan来生成scanplan

```c++
void
cstoreGetForeignPaths (PlannerInfo *root,
                RelOptInfo *baserel,
                Oid foreigntableid);
```
###### BeginForeignScan
```c++
void BeginForeignScan (ForeignScanState *node,
						int eflags);
```
在开始扫描之前做一些初始化的工作
注意：eflags & EXEC_FLAG_EXPLAIN_ONLY 为 true的时候，只做针对ExplainForeignScan和EndForeignScan的初始化工作。

CstoreGetforeignPlan 抽取出scanClause中的的regular clause（而不是pseudo constant clause）
只抽取需要的列，构造foreignPrivateList，这作为fdw_private

###### IterateForeignScan
```c++
TupleTableSlot * IterateForeignScan (
				ForeignScanState *node);
```
每次返回一行结果，其中TupleTableSlot中可能物理tuple或者虚拟tuple，虚拟cube是为了性能提升考虑的，直接引用了底层plannode的结果，省去一次数据拷贝。

###### ReScanForeignScan

重新扫描，由于参数可能变化，所以重新扫描和之前可能不一样

[funcs]: https://www.postgresql.org/docs/current/static/fdw-callbacks.html#FDW-CALLBACKS-SCAN
