---
layout: post
title: 
date: 2019-02-15 12:54
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---
> * TOC
{:toc}



# 总体结构

# 页头

# 行指针

# Tuple结构

## Tuple头

```c
typedef struct HeapTupleFields
{
	TransactionId t_xmin;		/* inserting xact ID */
	TransactionId t_xmax;		/* deleting or locking xact ID */

	union
	{
		CommandId	t_cid;		/* inserting or deleting command ID, or both */
		TransactionId t_xvac;	/* old-style VACUUM FULL xact ID */
	}			t_field3;
} HeapTupleFields;

struct HeapTupleHeaderData
{
	union
	{
		HeapTupleFields t_heap;
		DatumTupleFields t_datum;
	}			t_choice;

	ItemPointerData t_ctid;		/* current TID of this or newer tuple (or a
								 * speculative insertion token) */

	/* Fields below here must match MinimalTupleData! */

	uint16		t_infomask2;	/* number of attributes + various flags */

	uint16		t_infomask;		/* various flag bits, 比如是否有null值 */

	uint8		t_hoff;			/* sizeof header incl. bitmap, padding */

	/* ^ - 23 bytes - ^ */

	bits8		t_bits[FLEXIBLE_ARRAY_MEMBER];	/* bitmap of NULLs */

	/* MORE DATA FOLLOWS AT END OF STRUCT */
};
```

Tuple头部是由23byte固定大小的前缀和可选的NullBitMap构成。一个空行的大小是24byte，说明最后一个byte被对齐了；而一个有8个空值的行的长度也是24byte，说明最后一个byte当做了bitmap；到有超过8个空值后，那么就需要重新对齐。在Tuple数据中，不会存储Null数据。

```sql
postgres=# SELECT pg_column_size(row());
 pg_column_size
----------------
             24
(1 row)
postgres=# SELECT pg_column_size(row(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
 pg_column_size
----------------
             24
(1 row)
postgres=# SELECT pg_column_size(row(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,NULL));
 pg_column_size
----------------
             32
(1 row)
```

## Tuple数据

Tuple由多种类型构成。在PostgreSQL中，`pg_type`系统表中记录了各种类型的信息；通过如下查询，我们可以知道每个类型的传递方式（值or引用），长度以及对齐方式（和C结构体对齐相似）。

```sql
postgres=# select typname,typbyval,typlen,typalign from pg_type limit 3;
 typname | typbyval | typlen | typalign
---------+----------+--------+----------
 bool    | t        |      1 | c
 bytea   | f        |     -1 | i
 char    | t        |      1 | c
(3 rows)
```

> 长度列存在负数，负数是对应于变长类型。其中，-2对应的是cstring和unknown类型。-1对应的是其他变长类型。
>
> typalign有几种取值，分别对应不同长度的对齐：c（char，1），s（short，2），i（int，4），d（double，8）

### 对齐存储，节省空间

当你创建一个数据表时，通过检查`pg_type`中属性长度，合理安排属性顺序，可节省空间，如下例所示。

```sql
postgres=# CREATE TABLE t1 (a char , b int2 , c char , d int4 , e char , f int8);
CREATE TABLE
postgres=# CREATE TABLE t2 (f int8 , d int4 , b int2 , a char , c char , e char);
CREATE TABLE
postgres=# insert into t1 values ( 'a',1,'a',1,'a',1);
INSERT 0 1                      
postgres=# insert into t2 values ( 1,1,1,'a','a','a');
INSERT 0 1
postgres=# create extension pageinspect ;
CREATE EXTENSION
postgres=# select lp, t_data from
                     heap_page_items(get_raw_page('t1', 0));
 lp |                       t_data
----+----------------------------------------------------
  1 | \x056101000561000001000000056100000100000000000000
(1 row)

postgres=# select lp, t_data from
                     heap_page_items(get_raw_page('t2', 0));
 lp |                   t_data
----+--------------------------------------------
  1 | \x0100000000000000010000000100056105610561
(1 row)
```

### 变长属性列头部

对于变长的属性，在PostgreSQL页中会在前面加一个varlena结构，该结构中存储了磁盘中数据的存储方式（uncompressed，compressed和TOASTed）和实际的长度。

当变长数据的长度<=126 byte时，varlena的占1byte；当超过126byte时且没有被TOASTed时，varlena占4byte，如下例。

```sql
CREATE TABLE t1 (
	a varchar
);
insert into t1 values(repeat('a',126));
insert into t1 values(repeat('a',127));
select pg_column_size(a) from t1;
pg_column_size
---------------------
          127 
          131 
```

当varlena为1byte时，不会对齐；当为4byte时，会进行对齐，如下例；

```sql
postgres=# create table t5 (b varchar);
CREATE TABLE
postgres=# insert into t5 VALUES (''), (repeat('-',126)),(repeat('+',127));
INSERT 0 3
postgres=# select lp, t_data from heap_page_items(get_raw_page('t5', 0));
 lp |                                                                                                                                  t_data

----+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------
  1 | \x03
  2 | \xff2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d
2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d
  3 | \x0c0200002b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b
2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b
(3 rows)
```

> 那么，何时压缩数据？何时变toasted呢？

# 特殊空间

