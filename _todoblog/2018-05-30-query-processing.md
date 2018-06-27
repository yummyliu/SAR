---
layout: post
title: (译) Query Processing
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
typora-root-url: ../../yummyliu.github.io
---

# 第3章

​	在PG的官方文档中，PostgreSQL支持SQL2011的大部分特性；查询处理是其中最复杂的子系统，并且PostgreSQL的查询处理相当高效。本章概述下查询处理的过程，特别关注到查询的优化。

本章包括下面三个部分：

+ 第一部分：3.1节

		本节介绍了简述了PostgreSQL的查询处理。

+ 第二部分：3.2~3.4节

		本节描述了单表上获得最优计划的步骤。在3.2和3.3节，分别解释了代价估计过程和计划树的创建。在3.4节，简单描述了执行器的操作。

+ 第三部分：3.5~3.6节

  本节描述了多表上获得最优计划的步骤。在3.5节，介绍了三个join算法：nested loop，merge和hash连接。在3.6节，解释了多表上创建计划树的过程。

## 3.1 概览

在PostgreSQL中，尽管在9.6版本后，基于多个Background worker进程，有了并行查询；但是基本还是每个连接对应的一个Background Worker，他都是包括五个部分，如下：

1. Parser

   parser基于文本的SQL语句，生成ParserTree

   > 基于Flex，Bison实现的词法语法解析

2. Analyzer

   对ParserTree进行语义分析，生成QueryTree

   > 并且可以（但不一定做）一些权限检查和约束检查，一般放在执行的时候检查更全面

3. Rewriter

   基于[规则](https://www.postgresql.org/docs/current/static/rules.html)系统中存在的规则，对QueryTree进程重写

   > - 视图展开
   > - 常量计算
   > - 逻辑断言重写
   > - 语义优化，根据一些约束条件，修改语义
   > - 子查询展开

4. Planer

   基于QueryTree生成可以高效执行的PlanTree；

   > QueryTree描述了每个节点该做什么，PlanTree描述了每个节点具体的算法，比如使用IndexScan还是SeqScan）

5. Executor

   根PlanTree定义的顺序，访问表和索引，执行相应查询；

![QueryProcessing](/image/qp1.png)

<center>**图 3.1. Query Processing**</center>

在本节中，概述了这些子系统。由于planner和Executor很复杂，后面的章节会对这些函数的细节进行阐述。

> PostgreSQL的查询处理在[官方文档](http://www.postgresql.org/docs/current/static/overview.html)中有关于细节的阐述

#### 3.1.1 Parser

parser基于文本的SQL语句，产生了一个后续子系统可以理解的语法解析树。下面展示一个忽略细节的例子。

我们看一下下面这个查询。

```sql
testdb=# SELECT id, data FROM tbl_a WHERE id < 300 ORDER BY data;
```

语法解析树的根节点是一个`parsenodes.h`中定义的 `SelectStmt`类型。图3.2(b)展示了图3.2（a）对应的的语法解析树。

```c
typedef struct SelectStmt
{
        NodeTag         type;

        /*
         * These fields are used only in "leaf" SelectStmts.
         */
        List       *distinctClause;     /* NULL, list of DISTINCT ON exprs, or
                                         * lcons(NIL,NIL) for all (SELECT DISTINCT) */
        IntoClause *intoClause;         /* target for SELECT INTO */
        List       *targetList;         /* the target list (of ResTarget) */
        List       *fromClause;         /* the FROM clause */
        Node       *whereClause;        /* WHERE qualification */
        List       *groupClause;        /* GROUP BY clauses */
        Node       *havingClause;       /* HAVING conditional-expression */
        List       *windowClause;       /* WINDOW window_name AS (...), ... */

        /*
         * In a "leaf" node representing a VALUES list, the above fields are all
         * null, and instead this field is set.  Note that the elements of the
         * sublists are just expressions, without ResTarget decoration. Also note
         * that a list element can be DEFAULT (represented as a SetToDefault
         * node), regardless of the context of the VALUES list. It's up to parse
         * analysis to reject that where not valid.
         */
        List       *valuesLists;        /* untransformed list of expression lists */

        /*
         * These fields are used in both "leaf" SelectStmts and upper-level
         * SelectStmts.
         */
        List       *sortClause;         /* sort clause (a list of SortBy's) */
        Node       *limitOffset;        /* # of result tuples to skip */
        Node       *limitCount;         /* # of result tuples to return */
        List       *lockingClause;      /* FOR UPDATE (list of LockingClause's) */
        WithClause *withClause;         /* WITH clause */

        /*
         * These fields are used only in upper-level SelectStmts.
         */
        SetOperation op;                /* type of set op */
        bool            all;            /* ALL specified? */
        struct SelectStmt *larg;        /* left child */
        struct SelectStmt *rarg;        /* right child */
        /* Eventually add fields for CORRESPONDING spec here */
} SelectStmt;
```



![ParseTree](/image/fig-3-02.png)

<center>**图. 3.2. 语法解析树的例子**</center>

`SELECT`查询的元素和语法解析树的元素是相符的。比如，(1)是目标列表的一个元素，并且它是表的'id'列，(4)是一个`WHERE`语句，等等。

由于parser生成语法解析树的时候，只是检查语法，只有不符合语法的时候才会返回错误。

parser不检查任何查询的语义。比如，即使查询中有一个不存在的表名，parser也不会返回错误。语义检查是analyzer做的；

#### 3.1.2 Analyzer

Analyzer基于parser产出的语法解析树，进行语义分析，生成一个查询树；

```c
/*
 * Query -
 *	  Parse analysis turns all statements into a Query tree
 *	  for further processing by the rewriter and planner.
 *
 *	  Utility statements (i.e. non-optimizable statements) have the
 *	  utilityStmt field set, and the Query itself is mostly dummy.
 *	  DECLARE CURSOR is a special case: it is represented like a SELECT,
 *	  but the original DeclareCursorStmt is stored in utilityStmt.
 *
 *	  Planning converts a Query tree into a Plan tree headed by a PlannedStmt
 *	  node --- the Query structure is not used by the executor.
 */
typedef struct Query
{
	NodeTag		type;
	CmdType		commandType;		/* select|insert|update|delete|utility */
	QuerySource 	querySource;		/* where did I come from? */
	uint32		queryId;		/* query identifier (can be set by plugins) */

	bool		canSetTag;		/* do I set the command result tag? */
	Node	   	*utilityStmt;		/* non-null if this is DECLARE CURSOR or a non-optimizable statement */
	int		resultRelation; 	/* rtable index of target relation for INSERT/UPDATE/DELETE; 0 for SELECT */
	bool		hasAggs;		/* has aggregates in tlist or havingQual */
	bool		hasWindowFuncs; 	/* has window functions in tlist */
	bool		hasSubLinks;		/* has subquery SubLink */
	bool		hasDistinctOn;		/* distinctClause is from DISTINCT ON */
	bool		hasRecursive;		/* WITH RECURSIVE was specified */
	bool		hasModifyingCTE;	/* has INSERT/UPDATE/DELETE in WITH */
	bool		hasForUpdate;		/* FOR [KEY] UPDATE/SHARE was specified */
	bool		hasRowSecurity; 	/* row security applied? */
	List	   	*cteList;		/* WITH list (of CommonTableExpr's) */
	List	   	*rtable;		/* list of range table entries */
	FromExpr   	*jointree;		/* table join tree (FROM and WHERE clauses) */
	List	   	*targetList;		/* target list (of TargetEntry) */
	List	   	*withCheckOptions;	/* a list of WithCheckOption's */
	OnConflictExpr 	*onConflict; 		/* ON CONFLICT DO [NOTHING | UPDATE] */
	List	   	*returningList;		/* return-values list (of TargetEntry) */
	List	   	*groupClause;		/* a list of SortGroupClause's */
	List	   	*groupingSets;		/* a list of GroupingSet's if present */
	Node	   	*havingQual;		/* qualifications applied to groups */
	List	   	*windowClause;		/* a list of WindowClause's */
	List	   	*distinctClause; 	/* a list of SortGroupClause's */
	List	   	*sortClause;		/* a list of SortGroupClause's */
	Node	   	*limitOffset;		/* # of result tuples to skip (int8 expr) */
	Node	   	*limitCount;		/* # of result tuples to return (int8 expr) */
	List	   	*rowMarks;		/* a list of RowMarkClause's */
	Node	   	*setOperations;		/* set-operation tree if this is top level of a UNION/INTERSECT/EXCEPT query */
	List	   	*constraintDeps; 	/* a list of pg_constraint OIDs that the query
 depends on to be semantically valid */
} Query;
```

查询树的根是`parsenode.h`中定义的一个`Query`结构，这个结构包含了相应查询的元数据，比如命令的类型（SELECT/INSERT等），和一些叶子节点；每个叶子由一个列表或者树构成，包含了相应子句的数据。

![QueyTree](/image/fig-3-03.png)

<center>**图. 3.3 查询树的例子**</center>

简要描述下上述查询树：

+ targetlist就是查询的结果列的列表。在这个例子中，这个列表包含两个列：id和data。如果输入为`*`，那么Analyzer会将其明确替换为所有的列。
+ RangeTable是该查询用到的关系表。在这个例子中，这个RangeTable维护了表'tbl_a'的信息，比如表的oid和表的名字。
+ jointree保存了FROM和WHERE子句的信息
+ sortClauze是SortGroupClause的列表

QueryTree的细节在[官方文档](http://www.postgresql.org/docs/current/static/querytree.html)中有描述。

#### 3.1.3 Rewriter

rewriter实现了规则系统，必要的话，并根据存在pg_rules中的规则，转换查询树。规则系统本身是一个有趣的系统，但是本章中略去了关于规则系统和rewriter的描述，要不内容太长了。

> [View](https://www.postgresql.org/docs/current/static/rules-views.html)在PostgreSQL中是基于规则系统实现的。当使用`CREATE VIEW`创建一个视图，就会创建相应的rule，记在catalog中。
>
> 假设下面的view已经定义了，并在pg_rule中存了相应规则；
>
> ```sql
> sampledb=# CREATE VIEW employees_list 
> sampledb-#      AS SELECT e.id, e.name, d.name AS department 
> sampledb-#            FROM employees AS e, departments AS d WHERE e.department_id = d.id;
> ```
>
> 当发起了一个包含这个视图的查询，parser创建了一个如图. 3.4(a)的语法解析书。
>
> ```sql
> sampledb=# SELECT * FROM employees_list;
> ```
>
> 在这个阶段，rewriter会基于`pg_rules`存的view，将rangetable重写成一个子查询的语法解析树；
>
> ![rewriter](/image/fig-3-04.png)
>
> <center>图. 3.4(b)</center>
>
> 由于PostgreSQL基于这种机制实现了view，在9.2版本之前，view不能更新。但是，从9.3版本后，view可以更新；尽管如此，更新view有很多限制，具体细节在[官方文档](https://www.postgresql.org/docs/current/static/sql-createview.html#SQL-CREATEVIEW-UPDATABLE-VIEWS)中。

#### 3.1.4 Planner&Executor

planner从rewriter获取一个查询树，然后生成一个能被Executor高效执行的（查询）计划树。	

在PostgreSQL中，Planner是完全基于代价估计的；它不支持基于规则和基于hints的查询优化。planner是RDBMS中最复杂的部分，因此，本章的后续章节会做一个planner的概述。

> PostgreSQL不支持SQL中的hints，并且永远不会支持。如果你想在查询中使用hints，考虑使用`pg_hint_plan`扩展，详细内容参考[官方站点](http://pghintplan.osdn.jp/pg_hint_plan.html)。

和其他RDBMS类似，PostgreSQL中的`EXPLAIN`命令，展示了自己的计划树。例子如下：

```sql
testdb=# EXPLAIN SELECT * FROM tbl_a WHERE id < 300 ORDER BY data;
                          QUERY PLAN                           
---------------------------------------------------------------
 Sort  (cost=182.34..183.09 rows=300 width=8)
   Sort Key: data
   ->  Seq Scan on tbl_a  (cost=0.00..170.00 rows=300 width=8)
         Filter: (id < 300)
(4 rows)
```

这个`EXPLAIN`结果相应的计划树，如图3.5。

![planTree](/image/fig-3-05.png)

<center>图. 3.5一个简单的计划树以及其余EXPLAIN命令的关系</center>

​	每个PlanTree包括多个PlanNode，就是plantree列表中的每个*PlannedStmt*结构，其在`plannodes.h`中定义，细节在3.3.3节阐述。

​	每个PlanNode包括所有Executor需要的执行信息，在单表查询中，Executor就可以从下往上执行这个PlanTree。

​	比如，在图3.5中的计划树就是一个sort节点和seq scan节点的列表；因此，Executor基于seq scan方式扫描表，然后对得到的结果排序。

​	Executor通过第8章阐述的BufferManager，来访问数据库集群的表和索引。当处理一个查询时，Executor使用预先分配好的内存空间，比如*temp_buffers*和*work_mem*，必要的话还会创建临时文件。

​	另外，当访问元组的时候，PostgreSQL使用并发控制机制来维护执行的事务的一致性和隔离性。关于并发控制机制参考第五章。

![dd](/image/fig-3-06.png)

## 3.2 单表查询的代价估计

PostgreSQL是基于代价的查询优化。代价是一个无法准确定义的值，并且没有一个绝对的性能指标估计但是可以通过比较操作符性能的相对指标；

通过*costsize.c*中的函数，估计代价。所有的执行器执行的操作都有相应的代价函数。比如顺序扫描和索引扫描分别通过`cost_seqscan()` 和 `cost_index()`做代价估计。

在PostgreSQL中，有三种代价：**start_up**，**run**和**total**。total是start_up和run的和；因此，只有start_up和run是单独估计的。

1. **start_up**：在取到第一个tuple之前的代价，比如index-scan的start-up就是读取目标变的索引页，取到第一个元组的代价；
2. **run**： 获取全部的tuple的代价
3. **total**: 前两者的和

EXPLAIN命令展示了每个操作的start_up和run的代价。下面有个简单的例子：

```sql
testdb=# EXPLAIN SELECT * FROM tbl;
                       QUERY PLAN                        
---------------------------------------------------------
 Seq Scan on tbl  (cost=0.00..145.00 rows=10000 width=8)
(1 row)
```

在第4行中，这个命令展示了顺序扫描的信息。在代价部分，有两个值：0.00和145.00。这个例子中，start_up和total代价分别是0.00和145.00。

本节中，我们深入探究如何对顺序扫描，索引扫描和排序操作做代价估计。

在接下来的阐述中，我们使用下面说明的特定的表和索引：

```sql
testdb=# CREATE TABLE tbl (id int PRIMARY KEY, data int);
testdb=# CREATE INDEX tbl_data_idx ON tbl (data);
testdb=# INSERT INTO tbl SELECT generate_series(1,10000),generate_series(1,10000);
testdb=# ANALYZE;
testdb=# \d tbl
      Table "public.tbl"
 Column |  Type   | Modifiers 
--------+---------+-----------
 id     | integer | not null
 data   | integer | 
Indexes:
    "tbl_pkey" PRIMARY KEY, btree (id)
    "tbl_data_idx" btree (data)
```

#### 3.2.1 顺序扫描

通过`cost_seqscan()`函数，估计顺序扫描的代价。在这个部分，我们探究下面的查询如何估计顺序扫描的代价。

```sql
testdb=# SELECT * FROM tbl WHERE id < 8000;
```

在顺序扫描中，start_up代价等于0，run基于如下公式定义：
$$
‘run cost’ = ‘cpu\  run\ cost’ + ‘disk\ run\ cost’ =
(cpu\_tuple\_cost+cpu\_operator\_cost)\times N_{tuple} + seq\_page\_cost \times N_{page}
$$
其中seq_page_cost，cpu_tuple_cost和cpu_operator_cost在*postgresql.conf*配置，默认值分别是1.0，0.01和0.0025。$N_{tuple}$ 和$N_{page}$ 分别是表的所有元组和所有页，并且这些数字能够通过下面的查询获得:

```sql
testdb=# SELECT relpages, reltuples FROM pg_class WHERE relname = 'tbl';
 relpages | reltuples 
----------+-----------
       45 |     10000
(1 row)
```

$N_{tuple}=10000$ （1），

$N_{page} = 45$  (2)

因此：$run=(0.01+0.0025)×10000+1.0×45=170.0$ 

最终：$total = 0.0 + 170.0 = 170.0$

下面是上面查询的EXPLAIN命令的结果，我们验证一下：

```sql
testdb=# EXPLAIN SELECT * FROM tbl WHERE id < 8000;
                       QUERY PLAN                       
--------------------------------------------------------
 Seq Scan on tbl  (cost=0.00..170.00 rows=8000 width=8)
   Filter: (id < 8000)
(2 rows)
```

在第4行中，我们发现start-up和total代价分别是0.00和170.0，并且它估计通过全表扫描会得到8000行。

在第5行，展示了一个顺序扫描的过滤器'Filter:(id < 8000)'。更精确的是，它称为是一个*表级别谓词*。注意这种类型的过滤器是在读取所有元组的时候时候，它不会减少表物理页的扫描范围。

> 作为对run部分代价估计的理解，PostgreSQL假设所有的物理页都是从存储介质中拿到的；这意味着，PostgreSQL不考虑扫描的page是不是从shard buffer中取得的。

#### 3.2.2 索引扫描

尽管PostgreSQL支持很多索引类型，比如Btree，GiST，BIN和BRIN，但是索引扫描的代价估计都是使用公共的代价函数：`cost_index()`。

在这一节中，我们基于下面的查询，探究索引扫描的代价估计：

```sql
testdb=# SELECT id, data FROM tbl WHERE data < 240;
```

在估计这个代价之前，下面显示了$N_{index,page}$和$N_{index,tuple}$的数量：

```sql
testdb=# SELECT relpages, reltuples FROM pg_class WHERE relname = 'tbl_data_idx';
 relpages | reltuples 
----------+-----------
       30 |     10000
(1 row)
```

$N_{index,tuple} = 10000$，(3)

$N_{index,page} = 30$ (4)

##### 3.2.2.1 Start up 代价

索引扫描的启动代价就是读取索引页中，从而访问目标表的第一个元组的代价，基于下面的公式定义（怎么定的？）：
$$
start\ up\ cost = \{ceil(log_2(N_{index,tuple}))+(H_{index}+1)\times 50\}\times cpu\_operator\_cost
$$
这里$H_{index}$是索引树的高。

在这个情况下，根据(3)，$N_{index,tuple}$是10000；$H_{index}$是1；*cpu_operator_cost*是0.0025（默认值）。因此，
$$
start\ up\ cost = 0.285                    (5)
$$

##### 3.2.2.2 Run 代价

索引扫描的run代价是表和缩影的cpu代价和IO代价的和。
$$
run\ cost = (index\ cpu\ cost + table\ cpu\ cost) + (index\ io\ cost + table\ io\ cost)
$$

> 如果使用的是第7章中描述的Index-Only Scan，忽略*table cpu cost*和*table IO cost*。

前三个代价如下：

$index\ cpu\ cost = Selectivity \times N_{index,tuple} \times (cpu\_index\_tuple\_cost + qual\_op\_cost)$

$table\ cpu\ cost = Selectivity \times N_{tuple} \times cpu\_tuple\_cost$ 

$index IO cost = ceil(Selectivity \times N_{index,page} \times random\_page\_cost)$

以上的*cpu_index_tuple_cost*和*random_page_cost*在postgresql.conf配置（默认分别是0.005和4.0）；*qual_op_cost*粗略来讲就是index的代价，这里就不多展开了，默认值是0.0025。*Selectivity*是通过where子句得出的索引查找范围，是一个**[0.1]**的浮点数，如下；比如，$Selectivity \times N_{tuple}$ 就是需要读的表元组数量，$Selectivity \times N_{tuple}$就是需要读的索引元组数量等等。

> 选择率：
>
> 查询谓语的选择率是通过柱状图和MCV（众数）估计的，这些信息都在*pg_stats*中存着。这里基于特例，阐述一下选择率计算，细节可以看[官方文档](https://www.postgresql.org/docs/10/static/row-estimation-examples.html)。
>
> 每列的MCV存储在*pg_stats*视图的*most_common_vals*和*most_common_freqs*中
>
> + *most_common_vals*：该列上MCV的列表
> + *most_common_freqs*：MCV列表相应的频率
>
> 下面是一个简单的例子。表*countries*有两个列：一个存储country名字的country列和一个continent列；
>
> ```sql
> testdb=# \d countries
>    Table "public.countries"
>   Column   | Type | Modifiers 
> -----------+------+-----------
>  country   | text | 
>  continent | text | 
> Indexes:
>     "continent_idx" btree (continent)
>
> testdb=# SELECT continent, count(*) AS "number of countries", 
> testdb-#     (count(*)/(SELECT count(*) FROM countries)::real) AS "number of countries / all countries"
> testdb-#       FROM countries GROUP BY continent ORDER BY "number of countries" DESC;
>    continent   | number of countries | number of countries / all countries 
> ---------------+---------------------+-------------------------------------
>  Africa        |                  53 |                   0.274611398963731
>  Europe        |                  47 |                   0.243523316062176
>  Asia          |                  44 |                   0.227979274611399
>  North America |                  23 |                   0.119170984455959
>  Oceania       |                  14 |                  0.0725388601036269
>  South America |                  12 |                  0.0621761658031088
> (6 rows)
> ```
>
> 考虑一下，下面的带有WHERE条件`continent = 'Asia'`的查询：
>
> ```sql
> testdb=# SELECT * FROM countries WHERE continent = 'Asia';
> ```
>
> 这时候，planner使用continent列的MCV来估计索引扫描的代价，列的*most_common_vals* and *most_common_freqs* 如下：
>
> ```sql
> Expanded display is on.
> testdb=# SELECT most_common_vals, most_common_freqs FROM pg_stats 
> testdb-#                  WHERE tablename = 'countries' AND attname='continent';
> -[ RECORD 1 ]-----+-------------------------------------------------------------
> most_common_vals  | {Africa,Europe,Asia,"North America",Oceania,"South America"}
> most_common_freqs | {0.274611,0.243523,0.227979,0.119171,0.0725389,0.0621762}
> ```
>
> 和*most_common_vals* ： *Asia*值是对应的*most_common_freqs*是0.227979。因此，0.227979作为选择率的估计。
>
> 如果MCV不可用，就会使用目标列的*histogram_bounds*来估计代价。
>
> + **histogram_bounds**是将列的值划分为数量相同的一系列组。
>
> 一个特定的例子如下。这是表'tbl'中data列上的*histogram_bounds*值；
>
> ```sql
> testdb=# SELECT histogram_bounds FROM pg_stats WHERE tablename = 'tbl' AND attname = 'data';
>         			     	      histogram_bounds
> ---------------------------------------------------------------------------------------------------
>  {1,100,200,300,400,500,600,700,800,900,1000,1100,1200,1300,1400,1500,1600,1700,1800,1900,2000,2100,
> 2200,2300,2400,2500,2600,2700,2800,2900,3000,3100,3200,3300,3400,3500,3600,3700,3800,3900,4000,4100,
> 4200,4300,4400,4500,4600,4700,4800,4900,5000,5100,5200,5300,5400,5500,5600,5700,5800,5900,6000,6100,
> 6200,6300,6400,6500,6600,6700,6800,6900,7000,7100,7200,7300,7400,7500,7600,7700,7800,7900,8000,8100,
> 8200,8300,8400,8500,8600,8700,8800,8900,9000,9100,9200,9300,9400,9500,9600,9700,9800,9900,10000}
> (1 row)
> ```
>
> 默认，histogram_bounds划分为100个桶。图 3.7阐明了本例中的桶和相应的histogram_bounds。桶从0开始，每个桶保存了相同数量（大致相同）的元组。histogram_bounds的值就是相应桶的边界。比如，histogram_bounds的第0个值是1，意思是这是*bucket_0*中的最小值。第1个值是100，意思是bucket_1中的最小值是100，等等。
>
> 图3.7 桶和**histogram_bounds**
>
> ![](/image/fig-3-07.png)
>
> 接下来，本节例子中的选择率计算如下。查询有WHERE子句`data < 240`，并且值240在第二个bucket中。在这个例子中，可以利用*线性插值法*，得出选择性；因此，查询中data列的选择性使用下面的公式计算：
> $$
> Selectivity = \frac{2+(240-hb[2])/(hb[3]-hb[2])}{100}=\frac{2+(240-200)/(300-200)}{100}=\frac{2+40/100}{100}=0.024    \ (6)
> $$
>

因此，根据(1),(3),(4)和(6)，有
$$
'index\ cpu\ cost' = 0.024\times 10000 \times (0.005+0.0025)=1.8 \ (7) \\
'table\ cpu\ cost' = 0.024 \times 10000 \times 0.01 = 2.4 \ (8) \\
'index\ IO\ cost' = ceil(0.024 \times 30) \times 4.0 = 4.0 \ (9)
$$
$'table\ IO\ cost '$基于以下公式定义：
$$
'table\ IO\ cost' = max\_IO\_cost + indexCorerelation^2 \times (min\_IO\_cost-max\_IO\_cost)
$$
$max\_IO\_cost$是最差的IO代价，即，随机扫描所有数据页的代价；这个代价定义如下公式：
$$
max\_IO\_cost = N_{page}\times random\_page\_cost
$$
在本例中，由（2），$N_{page}=45$，得
$$
max\_IO\_cost = 45\times 4.0 = 180.0 \ (10)
$$
$min\_IO\_cost$是最优的IO代价，即，顺序扫描选定的数据页；这个代价定义如下公式：
$$
min\_IO\_cost = 1\times random\_page\_cost + (ceil(Selectivity\times N_{page})-1)\times seq\_page\_cost
$$
这个例子中，
$$
min\_IO\_cost \ = 1\times 4.0 + (ceil(0.024\times 45)-1)\times 1.0 \ (11)
$$
下文详细介绍$indexCorrelation$，在这个例子中，
$$
indexCorrelation = 1.0 \ (12)
$$
由（10），（11）和（12），得
$$
’table\ IO\ cost‘ = 180.0+1.0^2\times (5.0-180.0)=5.0 \ (13)
$$
综上，由（7），（8），（9）和（13）得
$$
’run\ cost‘ = (1.8+2.4)+(4.0+5.0)=13.2 \ (14)
$$

> 索引相关性（index correlation）
>
> 索引相关性是列值在物理上的顺序和逻辑上的顺序的统计相关性（引自官方文档）。范围从-1到+1。下面有一个具体的例子，帮助理解索引扫描和索引相关性的关系。
>
> 表*tbl_corr*有5个列：两个列式值类型，三个列是整数类型。这三个整数列保存从1到12的数字。物理上，表*tbl_corr*包含三个页，每个页有4个元组。每个数字列有一个，名字类似*index_col_asc*的索引。
>
> ```sql
> testdb=# \d tbl_corr
>     Table "public.tbl_corr"
>   Column  |  Type   | Modifiers 
> ----------+---------+-----------
>  col      | text    | 
>  col_asc  | integer | 
>  col_desc | integer | 
>  col_rand | integer | 
>  data     | text    |
> Indexes:
>     "tbl_corr_asc_idx" btree (col_asc)
>     "tbl_corr_desc_idx" btree (col_desc)
>     "tbl_corr_rand_idx" btree (col_rand)
> ```
>
> ```sql
> testdb=# SELECT col,col_asc,col_desc,col_rand 
> testdb-#                         FROM tbl_corr;
>    col    | col_asc | col_desc | col_rand 
> ----------+---------+----------+----------
>  Tuple_1  |       1 |       12 |        3
>  Tuple_2  |       2 |       11 |        8
>  Tuple_3  |       3 |       10 |        5
>  Tuple_4  |       4 |        9 |        9
>  Tuple_5  |       5 |        8 |        7
>  Tuple_6  |       6 |        7 |        2
>  Tuple_7  |       7 |        6 |       10
>  Tuple_8  |       8 |        5 |       11
>  Tuple_9  |       9 |        4 |        4
>  Tuple_10 |      10 |        3 |        1
>  Tuple_11 |      11 |        2 |       12
>  Tuple_12 |      12 |        1 |        6
> (12 rows)
> ```
>
> 这些列的索引相关性如下：
>
> ```sql
> testdb=# SELECT tablename,attname, correlation FROM pg_stats WHERE tablename = 'tbl_corr';
>  tablename | attname  | correlation 
> -----------+----------+-------------
>  tbl_corr  | col_asc  |           1
>  tbl_corr  | col_desc |          -1
>  tbl_corr  | col_rand |    0.125874
> (3 rows)
> ```
>
> 当执行下面的查询时，由于所有的目标元组只在第一个页中，PostgreSQL只会读取第一个页中，如图. 3.8(a)。
>
> ```sql
> testdb=# SELECT * FROM tbl_corr WHERE col_asc BETWEEN 2 AND 4;
> ```
>
> 另一方面，当执行下面的查询，PostgreSQL需要读所有的页，如图3.8(b)。
>
> ```sql
> testdb=# SELECT * FROM tbl_corr WHERE col_asc BETWEEN 2 AND 4;
> ```
>
> 如此，索引相关性是一种统计上的相关性，反映了在索引扫描代价估计中，由于索引顺序和物理元组顺序扭曲，导致的随机访问的影响。
>
> ###### 图. 3.8 索引相关性
>
> ![indexcor](/image/fig-3-08.png)

##### 3.2.2.3 整体代价

由（3）和（14），得
$$
'total\ cost' = 0.285 + 13.2 = 13.485 \ (15)
$$
确认下，上述SELECT查询的EXPLAIN结果如下所示：

```sql
testdb=# EXPLAIN SELECT id, data FROM tbl WHERE data < 240;
                                QUERY PLAN                                 
---------------------------------------------------------------------------
 Index Scan using tbl_data_idx on tbl  (cost=0.29..13.49 rows=240 width=8)
   Index Cond: (data < 240)
(2 rows)
```

在第4行，我们发现启动和整体的代价分别是0.29和13.49，并且估计有240行（元组）被扫描。

在第5行，指出了一个索引条件’Index Cond:(data < 240)‘。准确的说，这个条件是访问谓词，表示了索引扫描的开始和结束。

> > 基于[这篇文章](https://use-the-index-luke.com/sql/explain-plan/postgresql/filter-predicates)，PostgreSQL的EXPLAIN命令不区分访问谓词（access predicate）和索引过滤谓词（index filter predicate）。因此，如果分析EXPLAIN的输出，除了注意索引条件，也要注意估计的行数。

> **seq_page_cost和random_page_cost**
>
> seq_page_cost和random_page_cost的默认值分别是1.0和4.0。这意味着PostgreSQL假设随机扫描是顺序扫描的4倍；明显地，PostgreSQL的默认值是基于HDD设置的。
>
> 另一方面，最近广泛使用了SSD，random_page_cost的默认值有点太大。如果在SSD上，使用random_page_cost的默认值，planner会选择低效的的plans。因此，当使用SSD，最好将random_page_cost设置为1.0。
>
> [这篇文章](https://amplitude.engineering/how-a-single-postgresql-config-change-improved-slow-query-performance-by-50x-85593b8991b0)阐述了random_page_cost使用默认设置时的问题。

#### 单表查询的PlanTree创建

1. preprocessing
2. 基于代价估计最小代价的路径
3. 基于最小代价的路劲创建PlanTree

##### Preprocessing

1. 简化targetlist,limit clause；类似于把常量计算好

2. 标准化布尔操作 NOT(NOT a) = a

3. 离散逻辑AND OR，进行扁平化

   ![扁平化](/image/fig-3-09.png)

##### 基于代价估计最小代价路径

把每个节点上能做的所有操作的估计代价都算出来，然后看看哪个最短； 放在`PlannerInfo`中，然后基于所有的代价估计，给出一个最小的代价。有个细节，在每个节点上不同的路径，添加到PlannerInfo的时候，就是安装totalcost有序的方式添加的。

##### 基于最小代价的路劲创建PlanTree

数的根节点是一个`PlannedStmt`结构，而在PlannerInfo中，添加的path的时候会维护每个path之间的父子关系，这样每层找到一个最短路径，那么整个数就是一个最小代价的路径。

### Executor执行

在每个plannode中，都有一个对应的函数执行，代表该node的function；

## MultipleTable Plan

#### 预处理

1. 计划与转化CTE
2. 如果from中的子查询没有group by，having order by 等操作，上拉子查询
3. 可能的话，将外链接转化成内连接

####  得到代价最小的路径

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
