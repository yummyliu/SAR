---
layout: post
title: 
date: 2018-02-05 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
---

# Foreign data wrapper

## 使用FDW

在contrib中，自带一些fdw；

或者 https://wiki.postgresql.org/wiki/Foreign_data_wrappers

1. make && make install ，讲相应配置文件和库文件拷贝到PG的lib目录，
2. create extension … 加载这个插件

## 编写FDW

1. 实现两个C函数，**Handler**和 Validator (可选)。
2. 然后写一个control文件，control文件是在create extension加载的文件，根据这个文件，PG去寻找对应的sql文件，在 create extension … 的时候，执行相应的sql文件，来建立和数据库的关联。
3. 配置好上述 control文件和sql文件后，按照fdw的函数模板，编写动态链接库，即FDW插件。

### Handler

handler中是很多函数指针，用来匹配PG中执行计划执行。对于外表的scan是必须要实现的方法，同时也支持了对外表的其他操作，择需而定。

fdw的回调函数GetForeignRelSize, GetForeignPaths, GetForeignPlan, PlanForeignModify 必须和pg的planner运行机制相符。

##### 关键函数
###### GetForeignRelSize
```c
void GetForeignRelSize (PlannerInfo *root,
                   		RelOptInfo *baserel,
                   		Oid foreigntableid);
```
root：查询的全局信息
baserel：查询关于这个表的信息
foreigntableid：pg_class中关于这个外部表的Oid
该函数在制定外部表扫描计划的时候调用，利用baserel->baserestrictinfo来估算好表返回的元组数，更新baserel->rows的值

###### GetForeignPaths
```C
void GetForeignPaths (	PlannerInfo *root,
						RelOptInfo *baserel,
						Oid foreigntableid);
```
生成外部表扫描的路径，可能有多条路径，每条路径要有相应的cost。从进程的Recache中取到foreigntableid，对应的RelationData，double check

取到查询需要的列的列表，querycolimnlist
取到该表的数据文件的块大小 relationPageCount
取到该表的关系的属性总数，relationColumnCount

由上述信息得到查询列/总列数的比值 querycolumnratio，由此估算查询的页数，从而计算磁盘数据读取代价

最终总代价由，startucost+totalcpucost+totaldiskaccessCost
最后由上述得到的信息构造一个Path，ForeignPath

###### GetForeignPlan
```C
ForeignScan * GetForeignPlan (	PlannerInfo *root,
								RelOptInfo *baserel,
								Oid foreigntableid,
								ForeignPath *best_path,
								List *tlist,
								List *scan_clauses,
								Plan *outer_plan);
```
基于之前得到的path，得到ForeignScan计划。

###### BeginForeignScan
```c++
void BeginForeignScan (ForeignScanState *node,
						int eflags);
```
在开始扫描之前做一些初始化的工作
注意：eflags & EXEC_FLAG_EXPLAIN_ONLY 为 true的时候，只做针对ExplainForeignScan和EndForeignScan的初始化工作。

###### IterateForeignScan
```c++
TupleTableSlot * IterateForeignScan (
				ForeignScanState *node);
```
每次返回一行结果，其中TupleTableSlot中可能物理tuple或者虚拟tuple，虚拟cube是为了性能提升考虑的，直接引用了底层plannode的结果，省去一次数据拷贝。

###### ReScanForeignScan

重新扫描，由于参数可能变化，所以重新扫描和之前可能不一样
