---
layout: post
title: 
date: 2019-02-15 12:54
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - PostgreSQL
typora-root-url: ../../yummyliu.github.io
---
* TOC
{:toc}

# 总体结构

![image-20190216125128234](/image/image-20190216125128234.png)

PostgreSQL的堆表由多个页组成。业内结构如上图所示，由5部分构成，如下。

| 模块     | 描述                                                         |
| -------- | ------------------------------------------------------------ |
| 页头     | 24字节长，包含页内的大体信息与空闲空间的位置。               |
| 行指针   | 每个行指针占4个字节，由两项信息构成(offset,length) ，指向实际的Tuple数据。 |
| 空闲空间 | 页内未分配的空间，如果FreeSpace剩下的空间放不下一个元组，那么该页就是慢了。新的行指针从FreeSpace的头部开始分配，相应的Tuple数据从FreeSpace的尾部开始分配。 |
| Tuple    | 实际的Tuple数据                                              |
| 特殊空间 | 如果是索引页，那么根据索引类型的不同存储的数据也不同。       |

# 页头

```c
typedef struct PageHeaderData
{
	/* XXX LSN is member of *any* block, not only page-organized ones */
	PageXLogRecPtr pd_lsn;		/* LSN: next byte after last byte of xlog
								 * record for last change to this page */
	uint16		pd_checksum;	/* checksum */
	uint16		pd_flags;		/* flag bits, see below */
	LocationIndex pd_lower;		/* offset to start of free space */
	LocationIndex pd_upper;		/* offset to end of free space */
	LocationIndex pd_special;	/* offset to start of special space */
	uint16		pd_pagesize_version;
	TransactionId pd_prune_xid; /* oldest prunable XID, or zero if none */
	ItemIdData	pd_linp[FLEXIBLE_ARRAY_MEMBER]; /* line pointer array */
} PageHeaderData;

```

+ LSN：在BufferManager中，为了保证WAL的原则——*thou shalt write xlog before data*，对每个块标记了一个Log Sequence Number。
+ checksum：该项如果有值，就是该页的checksum；但是该项有时并没有设置。
+ prune xid：PostgreSQL中有一个对页内空间进行整理的过程，该列记录了上一个对页进行整理的xid。

# 行指针

```c
typedef struct ItemIdData
{
	unsigned	lp_off:15,		/* offset to tuple (from start of page) */
				lp_flags:2,		/* state of item pointer, see below */
				lp_len:15;		/* byte length of tuple */
} ItemIdData;
/*
 * lp_flags has these possible states.  An UNUSED line pointer is available
 * for immediate re-use, the other states are not.
 */
#define LP_UNUSED		0		/* unused (should always have lp_len=0) */
#define LP_NORMAL		1		/* used (should always have lp_len>0) */
#define LP_REDIRECT		2		/* HOT redirect (should have lp_len=0) */
#define LP_DEAD			3		/* dead, may or may not have storage */
```

包括偏移和长度两个信心，另外还有一个标记位，标记该行指针的状态。这里为了节省空间，在Struct中标记了**位域**，如下，行指针就只占4个字节了。

```c++
--------------
#include <iostream>
using namespace std;
struct ItemIdData
{
    unsigned    lp_off:15,        /* offset to tuple (from start of page) */
                lp_flags:2,        /* state of item pointer, see below */
                lp_len:15;        /* byte length of tuple */
} ItemIdData;

int main(int argc, char *argv[])
{
        cout << sizeof(struct ItemIdData) << endl;
        return 0;
}
------------------------------------------------------------
» ./a.out                                                                                                                liuyangming@liuyangmingdeMacBook-Air
4
```

# Tuple

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

那么，何时压缩数据？何时变toasted呢？

### Tuple压缩

当Tuple大小超过2000byte时，PostgreSQL会将Tuple中的变长数据，基于LZ压缩算法，进行压缩，如下。

```sql
postgres=# create table t6 (a varchar);
CREATE TABLE
postgres=# insert into t6 values (repeat('a',2004)),(repeat('a',2005));
INSERT 0 2
postgres=# select lp, t_data from heap_page_items(get_raw_page('t6', 0));
  1 | \x601f0000616161616161616161616161616161616161616161616161616...(省略号)
  2 | \x8e000000d5070000fe610f01ff0f01ff0f01ff0f01ff0f01ff0f01ff0f01ff010f014b
```

### TOAST

干杯属性的本意是*The Oversized-Attribute Storage Technique*，对于某个超长的属性单独存储。每个PostgreSQL类型都有一个存储类型，如下。

```c
postgres=# \d+ test
                                         Table "public.test"
 Column |       Type        | Collation | Nullable | Default | Storage  | Stats target | Description
--------+-------------------+-----------+----------+---------+----------+--------------+-------------
 a      | boolean           |           |          |         | plain    |              |
 b      | character varying |           |          |         | extended |              |
Publications:
    "alluserdata_pub"
```

可以看出来boolean类型的存储方式是plain，varchar的存储类型是extended。其中共有4种：

+ PLAIN ：避免压缩和行外存储。
+ EXTENDED ：先压缩，后行外存储。
+ EXTERNA ：允许行外存储，但不许压缩。
+ MAIN ：允许压缩，**尽量不使用**行外存储更贴切。

当某行数据超过PostgreSQL页大小（8k）后，会将这个页放到系统命名空间`pg_toast`下的一个单独的表中，而在原表中存储如下四个信息。

```c
typedef struct varatt_external
{
	int32		va_rawsize;		/* Original data size (includes header) */
	int32		va_extsize;		/* External saved size (doesn't) */
	Oid			va_valueid;		/* Unique ID of value within TOAST table */
	Oid			va_toastrelid;	/* RelID of TOAST table containing it */
}			varatt_external;
```

# 启发

+ 如果没有NULL值且没有变长字段，那么Tuple的长度是可以估计的；
+ 合理排列Tuple列，可以减少表占用空间。
  + 首先是Not NULL固定长度的属性。
  + 其次是合理排列固定长度的属性
  + 将所有变长列放到右边

以上，over。