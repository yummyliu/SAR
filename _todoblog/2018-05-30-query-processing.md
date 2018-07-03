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

### 3.1.1 Parser

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

### 3.1.2 Analyzer

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

### 3.1.3 Rewriter

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

### 3.1.4 Planner&Executor

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

### 3.2.1 顺序扫描

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

### 3.2.2 索引扫描

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

#### 3.2.2.1 Start up 代价

索引扫描的启动代价就是读取索引页中，从而访问目标表的第一个元组的代价，基于下面的公式定义（怎么定的？）：
$$
start\ up\ cost = \{ceil(log_2(N_{index,tuple}))+(H_{index}+1)\times 50\}\times cpu\_operator\_cost
$$
这里$H_{index}$是索引树的高。

在这个情况下，根据(3)，$N_{index,tuple}$是10000；$H_{index}$是1；*cpu_operator_cost*是0.0025（默认值）。因此，
$$
start\ up\ cost = 0.285                    (5)
$$

#### 3.2.2.2 Run 代价

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

#### 3.2.2.3 整体代价

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

### 3.2.3 排序

排序路径是在排序操作中使用的，比如ORDER BY，merge join的预处理等其他函数。排序的代价估计使用cost_sort()函数。

在排序操作中，如果能在`work_mem`中放下所有元组，那么就是用快速排序算法。否则，创建一个临时文件，使用外部归并排序。

排序计划路径的启动代价就是对目标表的排序代价，因此代价就是$O(N_{sort}\times log_2(N_{sort})$，这里$N_{sort}$就是需要排序的元组数。排序计划路径的运行代价就是读取已经排好序的元组的代价，因此代价就是$O(N_{sort})$。

在本小节中，我们探究如下查询的排序代价估计。假设这个查询只使用work_mem，不适用临时文件。

```sql
testdb=# SELECT id, data FROM tbl WHERE data < 240 ORDER BY id;
```

这个例子中，启动代价基于如下公式定义：
$$
‘start-up\  cost’ = C+comparison\_cost\times N_{sort} \times log_2(N_{sort})，
$$
这里$C$就是上一次扫描的总代价，即，索引扫描的代价；由（15）得13.485；$N_{sort}=240$；comparison_cost定义为$2\times cpu\_operator\_cost$。因此，
$$
‘start-up\ cost’ = 13.485+(2\times 0.0025)\times240.0\times log_2(240.0)=22.973
$$
运行代价是内存中读取排好序的元组的代价，即：
$$
‘run\ cost’=cpu\_operator\_cost\times N_{sort} = 0.0025\times 240 = 0.6
$$
综上：
$$
'total\ cost'=22.973+0.6=23.573
$$
确认一下，以上SELECT查询的EXPLAIN命令结果如下：

```sql
testdb=# EXPLAIN SELECT id, data FROM tbl WHERE data < 240 ORDER BY id;
                                   QUERY PLAN                                    
---------------------------------------------------------------------------------
 Sort  (cost=22.97..23.57 rows=240 width=8)
   Sort Key: id
   ->  Index Scan using tbl_data_idx on tbl  (cost=0.29..13.49 rows=240 width=8)
         Index Cond: (data < 240)
(4 rows)
```

在第4行，我们发现启动代价和运行代价分别是22.97和23.57。

## 3.3 创建单表查询的计划树

由于计划器特别复杂，本节描述最简单的情况，即，单表上的查询计划树的创建。更复杂的查询，换句话说就是多表上的查询计划树的创建在3.6节阐述。

PostgreSQL中的计划器有三个步骤，如下：

1. 预处理
2. 在所有可能的访问路径中，找出最小代价的路径
3. 基于最小代价的路径，创建查询计划树

访问路径是代价估计的一部分；比如，顺序扫描，索引扫描，排序以及多种连接操作都有其相应的路径。访问路径只在计划器创建查询计划树的时候使用。最基本的访问路径数据结构就是relation.h中定义的*Path*。它就相当于是顺序扫描。所有访问路径都是基于这个结构实现。后文会详细介绍其中你的细节。

```sql
typedef struct PathKey
{
        NodeTag         type;

        EquivalenceClass *pk_eclass;    /* the value that is ordered */
        Oid             pk_opfamily;    /* btree opfamily defining the ordering */
        int             pk_strategy;    /* sort direction (ASC or DESC) */
        bool            pk_nulls_first; /* do NULLs come before normal values? */
} PathKey;

typedef struct Path
{
	NodeTag		type;
	NodeTag		pathtype;	/* tag identifying scan/join method */
	RelOptInfo	*parent;	/* the relation this path can build */
	PathTarget 	*pathtarget;	/* list of Vars/Exprs, cost, width */
	ParamPathInfo   *param_info;	/* parameterization info, or NULL if none */
	bool		parallel_aware; /* engage parallel-aware logic? */
	bool		parallel_safe;	/* OK to use as part of parallel plan? */
	int		parallel_workers;/* desired # of workers; 0 = not parallel */
	/* estimated size/costs for path (see costsize.c for more info) */
	double		rows;		/* estimated number of result tuples */
	Cost		startup_cost;	/* cost expended before fetching any tuples */
	Cost		total_cost;	/* total cost (assuming all tuples fetched) */
	List	   	*pathkeys;	/* sort ordering of path's output */
	/* pathkeys is a List of PathKey nodes; see above */
} Path;
```

planner为了处理以上的步骤，内部创建一个PlannerInfo结构，维护查询树，查询中的关系表的相关信息，以及访问路径等等。

```c
typedef struct PlannerInfo
{
	NodeTag		type;
	Query	   	*parse;			/* the Query being planned */
	PlannerGlobal 	*glob;			/* global info for current planner run */
	Index		query_level;		/* 1 at the outermost Query */
	struct PlannerInfo *parent_root;	/* NULL at outermost Query */

	/*
	 * plan_params contains the expressions that this query level needs to
	 * make available to a lower query level that is currently being planned.
	 * outer_params contains the paramIds of PARAM_EXEC Params that outer
	 * query levels will make available to this query level.
	 */
	List		*plan_params;	/* list of PlannerParamItems, see below */
	Bitmapset  	*outer_params;

	/*
	 * simple_rel_array holds pointers to "base rels" and "other rels" (see
	 * comments for RelOptInfo for more info).  It is indexed by rangetable
	 * index (so entry 0 is always wasted).  Entries can be NULL when an RTE
	 * does not correspond to a base relation, such as a join RTE or an
	 * unreferenced view RTE; or if the RelOptInfo hasn't been made yet.
	 */
	struct RelOptInfo **simple_rel_array;	/* All 1-rel RelOptInfos */
	int		simple_rel_array_size;	/* allocated size of array */

	/*
	 * simple_rte_array is the same length as simple_rel_array and holds
	 * pointers to the associated rangetable entries.  This lets us avoid
	 * rt_fetch(), which can be a bit slow once large inheritance sets have
	 * been expanded.
	 */
	RangeTblEntry **simple_rte_array;	/* rangetable as an array */

	/*
	 * all_baserels is a Relids set of all base relids (but not "other"
	 * relids) in the query; that is, the Relids identifier of the final join
	 * we need to form.  This is computed in make_one_rel, just before we
	 * start making Paths.
	 */
	Relids		all_baserels;

	/*
	 * nullable_baserels is a Relids set of base relids that are nullable by
	 * some outer join in the jointree; these are rels that are potentially
	 * nullable below the WHERE clause, SELECT targetlist, etc.  This is
	 * computed in deconstruct_jointree.
	 */
	Relids		nullable_baserels;

	/*
	 * join_rel_list is a list of all join-relation RelOptInfos we have
	 * considered in this planning run.  For small problems we just scan the
	 * list to do lookups, but when there are many join relations we build a
	 * hash table for faster lookups.  The hash table is present and valid
	 * when join_rel_hash is not NULL.  Note that we still maintain the list
	 * even when using the hash table for lookups; this simplifies life for
	 * GEQO.
	 */
	List		*join_rel_list;	/* list of join-relation RelOptInfos */
	struct HTAB 	*join_rel_hash; /* optional hashtable for join relations */

	/*
	 * When doing a dynamic-programming-style join search, join_rel_level[k]
	 * is a list of all join-relation RelOptInfos of level k, and
	 * join_cur_level is the current level.  New join-relation RelOptInfos are
	 * automatically added to the join_rel_level[join_cur_level] list.
	 * join_rel_level is NULL if not in use.
	 */
	List	**join_rel_level;	/* lists of join-relation RelOptInfos */
	int	join_cur_level; 	/* index of list being extended */
	List	*init_plans;		/* init SubPlans for query */
	List	*cte_plan_ids;		/* per-CTE-item list of subplan IDs */
	List	*multiexpr_params;	/* List of Lists of Params for MULTIEXPR subquery outputs */
	List	*eq_classes;		/* list of active EquivalenceClasses */
	List	*canon_pathkeys; 	/* list of "canonical" PathKeys */
	List	*left_join_clauses;	/* list of RestrictInfos for
					 * mergejoinable outer join clauses w/nonnullable var on left */
	List	*right_join_clauses;	/* list of RestrictInfos for
					 * mergejoinable outer join clauses w/nonnullable var on right */
	List	*full_join_clauses;	/* list of RestrictInfos for mergejoinable full join clauses */
	List	*join_info_list; 	/* list of SpecialJoinInfos */
	List	*append_rel_list;	/* list of AppendRelInfos */
	List	*rowMarks;		/* list of PlanRowMarks */
	List	*placeholder_list;	/* list of PlaceHolderInfos */
	List	*fkey_list;		/* list of ForeignKeyOptInfos */
	List	*query_pathkeys; 	/* desired pathkeys for query_planner() */
	List	*group_pathkeys; 	/* groupClause pathkeys, if any */
	List	*window_pathkeys;	/* pathkeys of bottom window, if any */
	List	*distinct_pathkeys;	/* distinctClause pathkeys, if any */
	List	*sort_pathkeys;		/* sortClause pathkeys, if any */
	List	*initial_rels;		/* RelOptInfos we are now trying to join */

	/* Use fetch_upper_rel() to get any particular upper rel */
	List	*upper_rels[UPPERREL_FINAL + 1]; /* upper-rel RelOptInfos */

	/* Result tlists chosen by grouping_planner for upper-stage processing */
	struct PathTarget *upper_targets[UPPERREL_FINAL + 1];

	/*
	 * grouping_planner passes back its final processed targetlist here, for
	 * use in relabeling the topmost tlist of the finished Plan.
	 */
	List    *processed_tlist;

	/* Fields filled during create_plan() for use in setrefs.c */
	AttrNumber *grouping_map;	/* for GroupingFunc fixup */
	List	   *minmax_aggs;	/* List of MinMaxAggInfos */
	MemoryContext planner_cxt;	/* context holding PlannerInfo */
	double	   total_table_pages;	/* # of pages in all tables of query */
	double	   tuple_fraction; 	/* tuple_fraction passed to query_planner */
	double	   limit_tuples;	/* limit_tuples passed to query_planner */
	bool	   hasInheritedTarget;	/* true if parse->resultRelation is an inheritance child rel */
	bool	   hasJoinRTEs;		/* true if any RTEs are RTE_JOIN kind */
	bool	   hasLateralRTEs; 	/* true if any RTEs are marked LATERAL */
	bool	   hasDeletedRTEs; 	/* true if any RTE was deleted from jointree */
	bool	   hasHavingQual;	/* true if havingQual was non-null */
	bool	   hasPseudoConstantQuals; /* true if any RestrictInfo has pseudoconstant = true */
	bool	   hasRecursion;	/* true if planning a recursive WITH item */

	/* These fields are used only when hasRecursion is true: */
	int	   wt_param_id;	        /* PARAM_EXEC ID for the work table */
	struct Path *non_recursive_path;/* a path for non-recursive term */

	/* These fields are workspace for createplan.c */
	Relids	   curOuterRels;	/* outer rels above current node */
	List	   *curOuterParams; 	/* not-yet-assigned NestLoopParams */

	/* optional private data for join_search_hook, e.g., GEQO */
	void	   *join_search_private;
} PlannerInfo;
```

本节中，使用特定的例子描述如何从查询树中，创建查询计划树。

### 3.3.1 预处理

在创建一个计划树之前，planner对PlannerInfo中的查询树进行一些预处理。

尽管预处理有很多步，本小节中，我们只讨论和单表查询处理相关的主要步骤。其他的预处理操作在3.6节中描述。

1. 目标列表（target list）,和limit子句等，简单化；

   比如，通过clauses.c中的*eval_const_expressions()*，将'2+2'重写为4。

2. 布尔操作，标准化

   比如，'NOT(NOT a)'重写为 'a'

3. 离散逻辑AND/OR，扁平化

   SQL标准中的AND/OR是二元操作符；但是，在PostgreSQL内部，他们是多元操作符并且planner总是认为所有的嵌套AND/OR应该扁平化。举个特殊的例子。考虑一个布尔表达式'(id = 1) OR (id = 2) OR (id = 3)'，图3.9(a) 展示了使用二元表达式的查询树。通过使用三元表达式简化了这个查询树，如图3.9(b)。

   ###### 图3.9. 扁平布尔表达式的例子

   ![扁平化](/image/fig-3-09.png)

### 3.3.2 得到最小代价估计的访问路径

planner估计所有可能的访问路径的代价，然后选择代价最小的那个。具体来说，planner执行下面几个步骤：

```sql
typedef enum RelOptKind
{
	RELOPT_BASEREL,
	RELOPT_JOINREL,
	RELOPT_OTHER_MEMBER_REL,
	RELOPT_UPPER_REL,
	RELOPT_DEADREL
} RelOptKind;

typedef struct RelOptInfo
{
	NodeTag		type;
	RelOptKind	reloptkind;

	/* all relations included in this RelOptInfo */
	Relids		relids;			/* set of base relids (rangetable indexes) */

	/* size estimates generated by planner */
	double		rows;			/* estimated number of result tuples */

	/* per-relation planner control flags */
	bool		consider_startup;	/* keep cheap-startup-cost paths? */
	bool		consider_param_startup; /* ditto, for parameterized paths? */
	bool		consider_parallel;	/* consider parallel paths? */

	/* default result targetlist for Paths scanning this relation */
	struct PathTarget *reltarget;		/* list of Vars/Exprs, cost, width */

	/* materialization information */
	List	   *pathlist;			/* Path structures */
	List	   *ppilist;			/* ParamPathInfos used in pathlist */
	List	   *partial_pathlist;		/* partial Paths */
	struct Path *cheapest_startup_path;
	struct Path *cheapest_total_path;
	struct Path *cheapest_unique_path;
	List	   *cheapest_parameterized_paths;

	/* parameterization information needed for both base rels and join rels */
	/* (see also lateral_vars and lateral_referencers) */
	Relids		direct_lateral_relids;	/* rels directly laterally referenced */
	Relids		lateral_relids; 	/* minimum parameterization of rel */

	/* information about a base rel (not set for join rels!) */
	Index		relid;
	Oid		reltablespace;		/* containing tablespace */
	RTEKind		rtekind;		/* RELATION, SUBQUERY, or FUNCTION */
	AttrNumber	min_attr;		/* smallest attrno of rel (often <0) */
	AttrNumber	max_attr;		/* largest attrno of rel */
	Relids	   	*attr_needed;		/* array indexed [min_attr .. max_attr] */
	int32	   	*attr_widths;	   	/* array indexed [min_attr .. max_attr] */
	List	   	*lateral_vars;	   	/* LATERAL Vars and PHVs referenced by rel */
	Relids		lateral_referencers;	/* rels that reference me laterally */
	List	   	*indexlist;		/* list of IndexOptInfo */
	BlockNumber 	pages;			/* size estimates derived from pg_class */
	double		tuples;
	double		allvisfrac;
	PlannerInfo 	*subroot;		/* if subquery */
	List	   	*subplan_params; 	/* if subquery */
	int		rel_parallel_workers;	/* wanted number of parallel workers */

	/* Information about foreign tables and foreign joins */
	Oid		serverid;		/* identifies server for the table or join */
	Oid		userid;			/* identifies user to check access as */
	bool		useridiscurrent;	/* join is only valid for current user */
	/* use "struct FdwRoutine" to avoid including fdwapi.h here */
	struct FdwRoutine *fdwroutine;
	void	   	*fdw_private;

	/* used by various scans and joins: */
	List	   	*baserestrictinfo;	/* RestrictInfo structures (if base rel) */
	QualCost	baserestrictcost;	/* cost of evaluating the above */
	List	   	*joininfo;		/* RestrictInfo structures for join clauses involving this rel */
	bool		has_eclass_joins;	/* T means joininfo is incomplete */
} RelOptInfo;
```

1. 创建一个RelOptInfo结构，存储访问路径和相应的代价。

   通过make_one_rel()创建一个RelOptInfo结构，放在PlannerInfo的*simple_rel_array*中；如图3.10，在初始过程中，RelOptInfo维护了*baserestrictinfo*，如果相应索引存在，还有*indexlist*信息。baserestrictinfo就是查询的WHERE子句，indexlist存储目标表的相关索引。

2. 估计所有可能访问路径的代价，在RelOptInfo中添加访问路径。

   这一处理过程的细节如下：

   1.  创建一个路径，估计这个路径的顺序扫描的代价并写入到路径中，将该路径添加到RelOptInfo->pathlist中。
   2. 如果目标表存在相关的索引，创建一个索引访问路径。估计所有的索引扫描的代价并写入到路径中。然后将索引访问路径添加到pathlist中。
   3. 如何可以做位图扫描，创建一个位图扫描访问路径。估计所有的位图扫描的代价并写入到路径中。然后，将位图扫描路径添加到pathlist中。

3. 从RelOptInfo->pathlist中，找到最小代价的访问路径。

4. 可能的话，估计LIMIT, ORDER BY 和 ARREGISFDD的代价。

为了更加清晰的理解planner的工作，下面有两个特别的例子。

#### 3.3.2.1 例1

首先我们考察一个不带索引的简单单表查询；这个查询包含WHERE 和 ORDER BY子句。

```sql
testdb=# \d tbl_1
     Table "public.tbl_1"
 Column |  Type   | Modifiers 
--------+---------+-----------
 id     | integer | 
 data   | integer | 

testdb=# SELECT * FROM tbl_1 WHERE id < 300 ORDER BY data;
```

图3.10和3.11中，描述了本例中planner的处理。

###### 图. 3.10. 例1中如何得到最优路径

![](/image/fig-3-10.png)

1. 创建一个RelOptInfo结构，将其存在PlannerInfo->simple_rel_array中。

2. 在RelOptInfo的baserestrictinfo中，添加一个WHERE子句。

   通过initsplan.c中定义的*distribute_restrictinfo_to_rels()*，将id<300这个WHERE子句添加到baserestrictinfo中。另外，由于目标表没有相关索引，RelOptInfo的索引列表是NULL。

3. 为了排序需要，通过planner.c中的standard_qp_callback()行数，在PlannerInfo->sor_pathkeys中添加一个pathkey。

   *Pathkey*代表路径的排序顺序。本例中，因为order by的列是data，将data列添加到sort_pathkeys中，做为pathkey；

4. 创建一个path结构，并通过cost_seqscan函数估计顺序扫描的代价并写入到path中。然后，利用pathnode.c定义的add_path()函数，将这个path添加到RelOptInfo中。

   如同上面提到的，Path包含cost_seqscan函数估计的启动代价和总代价，等等。

   在本例中，目标表上没有索引，planner只估计了顺序扫描的代价；因此，最小代价自然而然决定了。

   ###### 图. 3.11. 如何得到例1中最小代价（接上图3.10.）

   ![](/image/fig-3-11.png)

5. 创建一个处理ORDER BY子句的新RelOptInfo结构

   注意新的RelOptInfo没有baserestrictinfo，这个结构是WHERE子句的信息。

6. 创建一个排序路径，并添加到新的RelOptInfo中；然后，将SortPath->subpath指向一个顺序扫描路径。

   ```c
   typedef struct SortPath
   {
   	Path	path;
   	Path	*subpath;		/* path representing input source */
   } SortPath;
   ```

   SortPath结构包括两个path结构：path和subpath；path存储sort操作符本身的信息，subpath存储最优访问路径。注意顺序扫描的path->parent指向，在baserestrictinfo中存储WHERE子句信息的老RelOptInfo。因此，因此，下一步，即创建计划树中，尽管新的RelOptInfo没有baserestrictinfo， planner可以创建一个包含WHERE条件作为Filter的顺序扫描节点；

   基于这里获得的最小代价访问路径，生成一个查询树。在3.3.3节中描述了相关细节。

#### 3.3.2.2 例2

下面探究一个包含两个索引的单表查询；这个查询包括两个WHERE子句。

```sql
testdb=# \d tbl_2
     Table "public.tbl_2"
 Column |  Type   | Modifiers 
--------+---------+-----------
 id     | integer | not null
 data   | integer | 
Indexes:
    "tbl_2_pkey" PRIMARY KEY, btree (id)
    "tbl_2_data_idx" btree (data)

testdb=# SELECT * FROM tbl_2 WHERE id < 240;
```

图3.12到3.14描述了planner处理这些例子。

###### 图. 3.12. 例2中得到最小代价的路径

![](/image/fig-3-12.png)

###### 图. 3.13. 例2中得到最小代价的路径（接图. 3.12）

![](/image/fig-3-13.png)

###### 图. 3.14. 例2中得到最小代价路径（接图. 3.13）

![](/image/fig-3-14.png)

1. 创建一个RelOptInfo结构

2. 在baserestrictinfo中，添加一个WHERE子句；并将目标表的索引添加到indexlist中。

   在本例中，在baserestrictinfo中，添加一个WHERE子句'id <240'，在RelOptInfo->indexlist中添加两个索引，*tbl_2_pkey*和*tbl_2_data_idx*；

3. 创建一个path，估计顺序扫描的代价并添加到RelOptInfo->indexlist中。

4. 创建一个IndexPath，估计索引扫描的代价，并使用add_path()函数，将IndexPath添加到RelOptInfo->pathlist中。

   ```sql
   typedef struct IndexPath
   {
   	Path		path;
   	IndexOptInfo 	*indexinfo;
   	List	   	*indexclauses;
   	List	   	*indexquals;
   	List	   	*indexqualcols;
   	List	   	*indexorderbys;
   	List	   	*indexorderbycols;
   	ScanDirection 	indexscandir;
   	Cost		indextotalcost;
   	Selectivity 	indexselectivity;
   } IndexPath;

   /*
    * IndexOptInfo
    *		Per-index information for planning/optimization
    *
    *		indexkeys[], indexcollations[], opfamily[], and opcintype[]
    *		each have ncolumns entries.
    *
    *		sortopfamily[], reverse_sort[], and nulls_first[] likewise have
    *		ncolumns entries, if the index is ordered; but if it is unordered,
    *		those pointers are NULL.
    *
    *		Zeroes in the indexkeys[] array indicate index columns that are
    *		expressions; there is one element in indexprs for each such column.
    *
    *		For an ordered index, reverse_sort[] and nulls_first[] describe the
    *		sort ordering of a forward indexscan; we can also consider a backward
    *		indexscan, which will generate the reverse ordering.
    *
    *		The indexprs and indpred expressions have been run through
    *		prepqual.c and eval_const_expressions() for ease of matching to
    *		WHERE clauses. indpred is in implicit-AND form.
    *
    *		indextlist is a TargetEntry list representing the index columns.
    *		It provides an equivalent base-relation Var for each simple column,
    *		and links to the matching indexprs element for each expression column.
    *
    *		While most of these fields are filled when the IndexOptInfo is created
    *		(by plancat.c), indrestrictinfo and predOK are set later, in
    *		check_index_predicates().
    */
   typedef struct IndexOptInfo
   {
   	NodeTag		type;
   	Oid		indexoid;		/* OID of the index relation */
   	Oid		reltablespace;		/* tablespace of index (not table) */
   	RelOptInfo 	*rel;			/* back-link to index's table */

   	/* index-size statistics (from pg_class and elsewhere) */
   	BlockNumber     pages;			/* number of disk pages in index */
   	double		tuples;			/* number of index tuples in index */
   	int		tree_height;		/* index tree height, or -1 if unknown */

   	/* index descriptor information */
   	int		ncolumns;		/* number of columns in index */
   	int		*indexkeys;		/* column numbers of index's keys, or 0 */
   	Oid		*indexcollations;	/* OIDs of collations of index columns */
   	Oid		*opfamily;		/* OIDs of operator families for columns */
   	Oid		*opcintype;		/* OIDs of opclass declared input data types */
   	Oid		*sortopfamily;		/* OIDs of btree opfamilies, if orderable */
   	bool	   	*reverse_sort;		/* is sort order descending? */
   	bool	   	*nulls_first;		/* do NULLs come first in the sort order? */
   	bool	   	*canreturn;		/* which index cols can be returned in an index-only scan? */
   	Oid		relam;			/* OID of the access method (in pg_am) */

   	List	   	*indexprs;		/* expressions for non-simple index columns */
   	List	   	*indpred;		/* predicate if a partial index, else NIL */

   	List	   	*indextlist;		/* targetlist representing index columns */

   	List	   	*indrestrictinfo;	/* parent relation's baserestrictinfo list,
   						 * less any conditions implied by the index's
   						 * predicate (unless it's a target rel, see
   						 * comments in check_index_predicates()) */

   	bool		predOK;			/* true if index predicate matches query */
   	bool		unique;			/* true if a unique index */
   	bool		immediate;		/* is uniqueness enforced immediately? */
   	bool		hypothetical;		/* true if index doesn't really exist */

   	/* Remaining fields are copied from the index AM's API struct: */
   	bool		amcanorderbyop;     	/* does AM support order by operator result? */
   	bool		amoptionalkey;		/* can query omit key for the first column? */
   	bool		amsearcharray;		/* can AM handle ScalarArrayOpExpr quals? */
   	bool		amsearchnulls;		/* can AM search for NULL/NOT NULL entries? */
   	bool		amhasgettuple;		/* does AM have amgettuple interface? */
   	bool		amhasgetbitmap; 	/* does AM have amgetbitmap interface? */
   	/* Rather than include amapi.h here, we declare amcostestimate like this */
   	void		(*amcostestimate) ();	/* AM's cost estimator */
   } IndexOptInfo;
   ```

   在本例中，有两个索引，tbl_2_pkey和tbl_2_data_index，这些索引是按顺序处理的。tbl_2_pkey首先处理。创建一个tbl_2_pkey的IndexPath，并估计启动代价和总代价。在这个例子中，tbl_2_pkey是id列相应的索引，并且WHERE包含这个id列；因此，WHERE子句存储在IndexPath的indexclauses中。

5. 创建另一个IndexPath，估计索引扫描的代价，将IndexPath添加到RelOptInfo->pathlist中。

   下一步，创建一个*tbl_2_data_idx*的IndexPath，估计这个IndexPath的代价，并加入到pathlist中。这里例子中，*tbl_2_data_idx*没有相关的WHERE子句；因此indexclauses是NULL。

6. 创建一个RelOptInfo结构

7. 将最小代价的路径，添加到新的RelOptInfo的->pathlist中。

   本例中，indexpath的最小代价路径是使用*tbl_2_pkey*；因此，将该路径添加到新的RelOptInfo中。

### 3.3.3 创建查询计划树

在最后一步中，planner基于最小代价的路径，生成一个计划树。 

计划树的根是定义在plannodes.h中的PlannedStmt结构，包含19个字段，如下是4个代表性字段：

+ **commandType**存储操作的类型，比如SELECT，UPDATE和INSERT。
+ **rtable**存储RangeTblEntry。
+ **relationOids**存储 查询相关表的oid。
+ **plantree**存储包含计划节点的计划树，每个计划节点对应一个特定操作，比如顺序扫描，排序和索引扫描。

```c
/* ----------------
 *		PlannedStmt node
 *
 * The output of the planner is a Plan tree headed by a PlannedStmt node.
 * PlannedStmt holds the "one time" information needed by the executor.
 * ----------------
 */
typedef struct PlannedStmt
{
	NodeTag		type;
	CmdType		commandType;		/* select|insert|update|delete */
	uint32		queryId;		/* query identifier (copied from Query) */
	bool		hasReturning;		/* is it insert|update|delete RETURNING? */
	bool		hasModifyingCTE;	/* has insert|update|delete in WITH? */
	bool		canSetTag;		/* do I set the command result tag? */
	bool		transientPlan;		/* redo plan when TransactionXmin changes? */
	bool		dependsOnRole;		/* is plan specific to current role? */
	bool		parallelModeNeeded;	/* parallel mode required to execute? */
	struct Plan 	*planTree;		/* tree of Plan nodes */
	List	   	*rtable;		/* list of RangeTblEntry nodes */
	/* rtable indexes of target relations for INSERT/UPDATE/DELETE */
	List	   	*resultRelations;       /* integer list of RT indexes, or NIL */
	Node	   	*utilityStmt;		/* non-null if this is DECLARE CURSOR */
	List	   	*subplans;		/* Plan trees for SubPlan expressions */
	Bitmapset  	*rewindPlanIDs;		/* indices of subplans that require REWIND */
	List	   	*rowMarks;		/* a list of PlanRowMark's */
	List	   	*relationOids;		/* OIDs of relations the plan depends on */
	List	   	*invalItems;		/* other dependencies, as PlanInvalItems */
	int		nParamExec;		/* number of PARAM_EXEC Params used */
} PlannedStmt;
```

 如上所述，计划树包含多种计划节点。PlanNode是基本的节点，其他节点都包含PlanNode。比如顺序扫描SeqScanNode，包含一个PlanNode和一个integer变量‘*scanrelid*’。PlanNode包含14个字段。下面是7个代表性字段：

+ startup_cost和total_cost是该节点对应操作的估计代价。
+ rows是planner估计的需要扫描的行数。
+ targetlist保存包含在这个查询树中的目标项列表。
+ qual存储等值条件的列表。
+ lefttree和righttree是以备添加子节点的节点。

```c
/* ----------------
 *		Plan node
 *
 * All plan nodes "derive" from the Plan structure by having the
 * Plan structure as the first field.  This ensures that everything works
 * when nodes are cast to Plan's.  (node pointers are frequently cast to Plan*
 * when passed around generically in the executor)
 *
 * We never actually instantiate any Plan nodes; this is just the common
 * abstract superclass for all Plan-type nodes.
 * ----------------
 */
typedef struct Plan
{
	NodeTag		type;
	/*
	 * estimated execution costs for plan (see costsize.c for more info)
	 */
	Cost		startup_cost;	/* cost expended before fetching any tuples */
	Cost		total_cost;	/* total cost (assuming all tuples fetched) */

	/*
	 * planner's estimate of result size of this plan step
	 */
	double		plan_rows;	/* number of rows plan is expected to emit */
	int		plan_width;	/* average row width in bytes */

	/*
	 * information needed for parallel query
	 */
	bool		parallel_aware; /* engage parallel-aware logic? */

	/*
	 * Common structural data for all Plan types.
	 */
	int		plan_node_id;	/* unique across entire final plan tree */
	List	   	*targetlist;	/* target list to be computed at this node */
	List	   	*qual;		/* implicitly-ANDed qual conditions */
	struct Plan 	*lefttree;	/* input plan tree(s) */
	struct Plan 	*righttree;
	List	   	*initPlan;	/* Init Plan nodes (un-correlated expr subselects) */
	/*
	 * Information for management of parameter-change-driven rescanning
	 *
	 * extParam includes the paramIDs of all external PARAM_EXEC params
	 * affecting this plan node or its children.  setParam params from the
	 * node's initPlans are not included, but their extParams are.
	 *
	 * allParam includes all the extParam paramIDs, plus the IDs of local
	 * params that affect the node (i.e., the setParams of its initplans).
	 * These are _all_ the PARAM_EXEC params that affect this node.
	 */
	Bitmapset	*extParam;
	Bitmapset  	*allParam;
} Plan;
```

```c
/*
 * ==========
 * Scan nodes
 * ==========
 */
typedef unsigned int Index;

typedef struct Scan
{
	Plan		plan;
	Index		scanrelid;		/* relid is index into the range table */
} Scan;

/* ----------------
 *		sequential scan node
 * ----------------
 */
typedef Scan SeqScan;
```

下面描述了，基于前小节的最小代价路径，生成的两个计划树。

#### 3.3.3.1. 例1

第一个例子是3.3.2.1节的例子的计划树。图. 3.11中展示的最小代价路径是排序路径和顺序扫描路径的结合；根节点是排序路径，子路径是顺序扫描路径。尽管忽略了细节的解释，但是很容易理解计划树可以从最小代价路径中简单生成。本例中，将SortNode添加到PlannedStmt结构中，并将SeqScanNode添加到SortNode的左子树中，如图.3.15(a)。

```c
typedef struct Sort
{
	Plan		plan;
	int		numCols;		/* number of sort-key columns */
	AttrNumber 	*sortColIdx;		/* their indexes in the target list */
	Oid		*sortOperators;		/* OIDs of operators to sort them by */
	Oid		*collations;		/* OIDs of collations */
	bool	   	*nullsFirst;		/* NULLS FIRST/LAST directions */
} Sort;
```

###### 图. 3.15. 计划树的例子

![](/image/fig-3-15.png)

在SortNode中，左子树指向SeqScanNode。在SeqScanNode中，qual保存WHERE子句'id<300'。

#### 3.3.3.2 例2

第一个例子是3.3.2.2节的例子的计划树。图. 3.14中展示的最小代价路径是索引扫描路径；因此，计划树只有IndexScanNode自己组成，如图3.15(b)。

```c
/* ----------------
 *		index scan node
 *
 * indexqualorig is an implicitly-ANDed list of index qual expressions, each
 * in the same form it appeared in the query WHERE condition.  Each should
 * be of the form (indexkey OP comparisonval) or (comparisonval OP indexkey).
 * The indexkey is a Var or expression referencing column(s) of the index's
 * base table.  The comparisonval might be any expression, but it won't use
 * any columns of the base table.  The expressions are ordered by index
 * column position (but items referencing the same index column can appear
 * in any order).  indexqualorig is used at runtime only if we have to recheck
 * a lossy indexqual.
 *
 * indexqual has the same form, but the expressions have been commuted if
 * necessary to put the indexkeys on the left, and the indexkeys are replaced
 * by Var nodes identifying the index columns (their varno is INDEX_VAR and
 * their varattno is the index column number).
 *
 * indexorderbyorig is similarly the original form of any ORDER BY expressions
 * that are being implemented by the index, while indexorderby is modified to
 * have index column Vars on the left-hand side.  Here, multiple expressions
 * must appear in exactly the ORDER BY order, and this is not necessarily the
 * index column order.  Only the expressions are provided, not the auxiliary
 * sort-order information from the ORDER BY SortGroupClauses; it's assumed
 * that the sort ordering is fully determinable from the top-level operators.
 * indexorderbyorig is used at runtime to recheck the ordering, if the index
 * cannot calculate an accurate ordering.  It is also needed for EXPLAIN.
 *
 * indexorderbyops is a list of the OIDs of the operators used to sort the
 * ORDER BY expressions.  This is used together with indexorderbyorig to
 * recheck ordering at run time.  (Note that indexorderby, indexorderbyorig,
 * and indexorderbyops are used for amcanorderbyop cases, not amcanorder.)
 *
 * indexorderdir specifies the scan ordering, for indexscans on amcanorder
 * indexes (for other indexes it should be "don't care").
 * ----------------
 */
typedef struct Scan
{
        Plan        plan;
        Index       scanrelid;          /* relid is index into the range table */
} Scan;

typedef struct IndexScan
{
	Scan	   scan;
	Oid	   indexid;		/* OID of index to scan */
	List	   *indexqual;		/* list of index quals (usually OpExprs) */
	List	   *indexqualorig;	/* the same in original form */
	List	   *indexorderby;	/* list of index ORDER BY exprs */
	List	   *indexorderbyorig;	/* the same in original form */
	List	   *indexorderbyops;	/* OIDs of sort ops for ORDER BY exprs */
	ScanDirection indexorderdir;	/* forward or backward or don't care */
} IndexScan;
```

在这个例子中，WHERE子句'id<240'是一个访问谓词；因此，其存储在IndexScanNode的indexqual中。

每个

### 3.4 Executor如何执行

在单表查询中，执行器从下至上执行计划节点，并调用相应节点的处理函数。

每个计划节点有执行相应操作的函数，这些函数在src/backend/executor目录中。比如，执行顺序扫描的的函数（SeqScan）在nodeSeqscan.c中；执行索引扫描的函数（IndexScanNode）定义在nodeIndexScan.c中；SortNode节点的排序函数定义在nodeSort.c中等等。

当然，理解excutor最好方式就是读EXPLAIN命令的输出，因为PostgreSQL的EXPLAIN命令几乎就表示了计划树。下面解释一下3.3.3节的例1。

```c
testdb=# EXPLAIN SELECT * FROM tbl_1 WHERE id < 300 ORDER BY data;
                          QUERY PLAN                           
---------------------------------------------------------------
 Sort  (cost=182.34..183.09 rows=300 width=8)
   Sort Key: data
   ->  Seq Scan on tbl_1  (cost=0.00..170.00 rows=300 width=8)
         Filter: (id < 300)
(4 rows)
```

一起从下往上读一下EXPLAIN的结果，探究executor如何执行的：

line 6: 首先，Executor执行nodeSeqscan.c中定义的顺序扫描操作。

line 4：接下来，Executor使用nodeSort.c中定义的函数，对顺序扫描的结果进行排序。

> 临时文件
>
> 尽管Executor使用内存中分配的work_mem和temp_buffers，但是如果查询处理中内存不够，就会使用临时文件。
>
> 使用analyze选项，EXPLAIN会执行这个查询并展示真正的行数，实际执行时间和实际内存利用。如下有个特定的例子：
>
> ```c
> testdb=# EXPLAIN ANALYZE SELECT id, data FROM tbl_25m ORDER BY id;
>                                                         QUERY PLAN                                                        
> --------------------------------------------------------------------------------------------------------------------------
>  Sort  (cost=3944070.01..3945895.01 rows=730000 width=4104) (actual time=885.648..1033.746 rows=730000 loops=1)
>    Sort Key: id
>    Sort Method: external sort  Disk: 10000kB
>    ->  Seq Scan on tbl_25m  (cost=0.00..10531.00 rows=730000 width=4104) (actual time=0.024..102.548 rows=730000 loops=1)
>  Planning time: 1.548 ms
>  Execution time: 1109.571 ms
> (6 rows)
> ```
>
> 在第6行，EXPLAIN命令显示了执行器使用了10000KB的临时文件。
>
> 临时文件临时创建在base/pg_tmp子目录中，遵循如下命名规则
>
> ```bash
> {"pgsql_tmp"} + {PID of the postgres process which creates the file} . {sequencial number from 0}
> ```
>
> 比如，临时文件pgsql_tmp8903.5是pid为8903的postgres进程创建的第6个临时文件



