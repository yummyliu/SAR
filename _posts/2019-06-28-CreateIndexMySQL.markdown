---
layout: post
title: MySQL源码——Online Create Index
date: 2019-06-28 18:10
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - MySQL
typora-root-url: ../../yummyliu.github.io
---

* TOC
{:toc}


# 简述

在MySQL 5.7中，按照如下语法可以创建三种索引：btree、fulltext、spatial。

```sql
CREATE [UNIQUE | FULLTEXT | SPATIAL] INDEX index_name
    [index_type]
    ON tbl_name (key_part,...)
    [index_option]
    [algorithm_option | lock_option] ...
```

在index_type中有btree和hash可选；但是根据存储引擎的不同，有所区别。

| Storage Engine                                               | Permissible Index Types            |
| ------------------------------------------------------------ | ---------------------------------- |
| [`InnoDB`](innodb-storage-engine.html)                       | `BTREE`                            |
| [`MyISAM`](storage-engines.html#myisam-storage-engine)       | `BTREE`                            |
| [`MEMORY`](storage-engines.html#memory-storage-engine)/`HEAP` | `HASH`, `BTREE`                    |
| [`NDB`](mysql-cluster.html)                                  | `HASH`, `BTREE` (see note in text) |

我们以InnoDB为例，InnoDB不支持Hash。另外，当你创建的是fulltext和spatial索引时，index_type就不能指定了。因为fulltext的实现一般是倒排表，也根据存储引擎有关；spatial一般是R-tree。

————————————————————————————————————

## 整体数据组织

![img](/image/InnoDB数据组织.png)

创建索引涉及了InnoDB中的内存和外存的数据组织，比如dictionary cache，block/page/frame等分别是什么意思：

![image-20190701113428176](/image/image-20190701113428176.png)

dictionary cache是全局共享对象的cache，比如：

1. tablespace defintion
2. schema definition
3. table definition
4. stored program definition
5. character set definition
6. collation definition

在InnoDB中，frame代表内存的虚拟地址空间的一个16k单元，主要用在缓冲区管理中；page代表物理内存的一个16k单元，其中是需要写回到磁盘的数据；而Block代表一个Control Block（`buf_block_t`），对于每个frame对应一个ControlBlock结构进行控制信息管理，但这些信息不写回内存。

![innodb files](/image/innodb-tablespace.png)

在外存中，按照表空间（space）进行组织；当启动了`innodb_file_per_table`参数后，每个数据表对应一个文件（参看系统表：INFORMATION_SCHEMA.INNODB_SYS_DATAFILES）。但是全局共享的对象，需要放在共享的表空间space0（全局变量`：innodb_data_file_path，默认为ibdata1）中：

+ data dictionary（InnoDB表的元信息）
+ change buffer
+ double write buffer
+ undo log

每个space有若干个file或者diskpartition；其中spaceid=0为系统表空间，其中有一些全局共享的结构：

+ 公共结构体：ibuf、trx、dict等
+ doublewrite buffer
+ 数据目录

每个file分为若干个segment；其中有LeafNodeSegment、NonLeafNodeSegment、rollbacksegment，见[General Tablespace](https://dev.mysql.com/doc/refman/5.7/en/general-tablespaces.html)。

每个segment中有若干个固定大小的page

+ 对于uncompressed的表空间，page是16Kb；

+ 对于compressed的表空间，page是1~16Kb。

> IBUF_TREE
>
> Insert Buffer是二级索引变更的缓存；之前只有insert操作，后来也支持了delete/update/purge操作，成为ChangeBuffer；但是命名上没有改变，在内存中还是叫InsertBuffer；在外存中，放在ibuf文件中。

————————————————————————————————————

版本<5.5，创建一个索引相当于重建一个表（**CopyTable**）。

版本>=5.5，加入了FastIndexCreate特性，但只对二级索引有效（**Inplace**）；索引中只有发起createindex时刻的数据，create index时只能读不能写。过程有主要有如下三步：

1. 扫描表，建立内存buffer和临时文件
2. 进行合并排序
3. 将行插入到索引中

版本>=5.6.7，加入了Online Create Index的特性，创建二级索引的时候可读可写（还是会短暂block一下，但是已经影响很小了）；对于创建索引过程中对表进行的修改，放在RowLog（不是redolog）中；

————————————————————————————————————

InnoDB Instrinsic Tables：InnoDB引擎内部的表，没有undo和redo，用户不可创建，只供引擎内部使用。

+ Index Page的物理结构

![image-20190718113614074](/image/INDEX_Page_Overview.png)

+ Index Page的逻辑结构

  ![image-20190530163445011](/image/innodb-page-directory.png)

> dict_index_t->n_uniq：在dict_index_t->n_fields中，从前向后，足够用来判断键唯一的列数。
>
> **n_owned**
>
> 在InnoDB中，表是按照B+Tree的方式存储。每个页中，都有一个PageDirectory(如下图)。其中保存了该页内的record的偏移量。由于不是每个record在PageDirectory中都有一个slot（这称为sparse slots），因此每个record中就有了一个n_owned变量，保存了该recored所own的record数，类似于前向元组数。

# 创建过程

![image-20190814100917780](/image/online-create-index.png)

入口函数是`mysql_inplace_alter_table`。分为以下几步：

1. `mdl_context.upgrade_shared_lock`；创建一个Rowlog，并等待该表上的事务结束；此后开启的新事务的修改放在Rowlog中。
2. `ha_prepare_inplace_alter_table`；写元数据，并建立root节点
   - row_merge_create_index：插入系统表关于该索引的元信息。
   - btr_create：创建root节点。
3. `downgrade_lock`：如果存储引擎返回不需要xlock或者slock，那么可以降级为shared metadata lock。
4. `ha_inplace_alter_table`；构建索引
   1. 遍历一级索引，对索引列进行归并排序
   2. 将排序好的数据，填充到索引中。
   3. 应用RowLog
5. 收尾工作：ha_commit_inplace_alter_table等。

## 准备系统信息与结构

*ha_prepare_inplace_alter_table*

### 添加系统元信息

在dict0crea.h的dict_create_index_step中，定义了初始化index的4步操作：

1. **INDEX_BUILD_INDEX_DEF**：dict_build_index_def_step，构造一个索引的定义元组（后续将插入到系统表**INFORMATION_SCHEMA.INNODB_SYS_INDEXES**中，具体内容在`dict_create_sys_indexes_tuple`函数中）。

   + ```
            INDEX_ID: 40
                NAME: i1
            TABLE_ID: 34
                TYPE: 0
            N_FIELDS: 1
             PAGE_NO: 4
               SPACE: 23
     MERGE_THRESHOLD: 50
     ```

2. **INDEX_BUILD_FIELD_DEF**：dict_build_field_def_step，基于要创建的索引，构造一个系统表:`INFORMATION_SCHEMA.INNODB_SYS_FIELDS`的元组。取决于索引列有几个，该步骤执行多少次。

   ```sql
   INDEX_ID: 117
       NAME: col1
        POS: 0
   ```

   > 这里系统表的插入模式是INS_DIRECT，
   >
   > ```c
   > /* Insert node types */
   > #define INS_SEARCHED	0	/* INSERT INTO ... SELECT ... */
   > #define INS_VALUES	1	/* INSERT INTO ... VALUES ... */
   > #define INS_DIRECT	2	/* this is for internal use in dict0crea:
   > 				insert the row directly */
   > ```

3. **INDEX_ADD_TO_CACHE**：dict_index_add_to_cache_w_vcol，将索引添加到**<u>dictionarycache</u>**中

4. **INDEX_CREATE_INDEX_TREE**：dict_create_index_tree_step，创建索引

   1. 检查表空间与文件是否存在
   2. 初始化btree的cursor
   3. 创建root节点：btr_create
   4. 更新系统表sys_indexes的page_no字段



### 创建Btree根节点

> <u>btr_create</u>

先不考虑`type&DICT_IBUF==True`的情况：

1. fseg_create

   创建两个segment段

   + 分配一个Non-leaf段（PAGE_BTR_SEG_TOP ），段首部放在本段的page中。
   + 分配一个leaf段（PAGE_BTR_SEG_LEAF ），段首部放在非叶子节点的段中。

2. page_create：在non-leaf段中，分配一个page；

3. 初始化跟节点信息：level/indexid/next/prev等。

如果type为IBUF_TREE，那么在单独空间中，额外申请一个ibuf的segment。并且只有在`fsp_header_init`（**段空间初始化**）且**spaceid=0**的时候才会将设置IBUF_TREE。

## 加载索引数据

*ha_inplace_alter_table->row_merge_build_indexes*；加载索引数据时，需要在mergefile上进行外部归并排序，因为要进行多次每个阶段的输出在一个临时文件tmpfile中；在排序之前先申请一个内存缓存区block（mergefilebuffer）；

1. 创建mergefile的内存缓冲区block，并初始化；

   ```c
   	block = alloc.allocate_large(3 * srv_sort_buf_size, &block_pfx);
   ```

2. 根据索引类型设置**FLushObserver**（spatialIndex写redo日志，就不需要这个标记）

   ，因为批量加载数据，不记redo日志；但在进行应用rowlog之前，需要将脏页刷盘。

   > 1. online add index: flush dirty pages right before row_log_apply().
   > 2. table rebuild: flush dirty pages before row_log_table_apply().

3. 读取一级索引前的初始化工作

   + 初始化mergefile的fileDescriptor：`merge_files[i].fd = -1;`
   + 重置rowbuffer（这是用来确认是不是有重复值）:`		table->field[i]->set_default();`

4. 读取一级索引，**创建分区有序的mergefile**：`row_merge_read_clustered_index`（见下一节）。

5. **外部归并排序**：`row_merge_sort`

6. **填充新索引**：`row_merge_insert_index_tuples`

7. **应用RowLog**：

   1. 清理临时文件，释放表空间
   2. flushobverser->flush
   3. 写入一个**MLOG_INDEX_LOAD** 记录，表示相应index的page不写redo，但是已经刷盘完成。
   4. row_log_apply

### 创建分区有序的mergefile

*row_merge_read_clustered_index*

合并排序之前，进行读取并产生分别拍好序的分片。这里不考虑FTS和spatial索引。

1. 创建并初始化record的内存缓存

   merge_buf[i] = row_merge_buf_create(index[i]);

2. 查找一级索引，并创建一个持久化的cursor。

   ```
   	clust_index = dict_table_get_first_index(old_table);
   	btr_pcur_open_at_index_side(
   		true, clust_index, BTR_SEARCH_LEAF, &pcur, true, 0, &mtr);
   ```

   btr_pcur_open_at_index_side第一个参数为true，表示cursor从左侧（排序头）开始。

   创建特定的内存堆区域：

   + row_heap: 用来创建聚集索引的tuple。
   + conv_heap：当需要调用row_merge_buf_redundant_convert，将行模式转变为REDUNDANT。
   + mtuple_heap：merge data tuple；在`row_mtuple_create`中用来放merge的tuple。

3. 扫描聚集索引

   **定位cursor**

   1. cursor在页内向后迭代

   2. 判断是不是页内最后一个record

      1. 是的话，判断是否有其他线程在等待该聚集索引的rwlock，有则暂停本线程，之后再重新开始。

         ```c
         				btr_pcur_store_position(&pcur, &mtr);
         				mtr_commit(&mtr);
         		    ...
         				/* Give the waiters a chance to proceed. */
         				os_thread_yield();
         				
         				btr_pcur_restore_position(
         					BTR_SEARCH_LEAF, &pcur, &mtr);
         ```

      迭代到下一页：btr_page_get_next。

   ————————————————————————————————————

   **取出record**

   1. 读取clusteredIndex的记录

   2. 判读数据可见性：如果online createindex，那么通过`row_vers_build_for_consistent_read`构建可重复读的readview；否则，只是通过`rec_get_deleted_flag`判断数据是否标记删除了。

      因此，在MySQL中二级索引是简介的查询，需要[两次index lookup](https://stackoverflow.com/questions/56609196/innodb-secondary-index-includes-value-instead-of-pointer-to-pk-how-is-it-enough)。由于缓存的是值，因此文档中，不建议主键设置过大。

      > [Clustered and Secondary Indexes](https://dev.mysql.com/doc/refman/5.7/en/innodb-index-types.html)
      >
      > If the primary key is long, the secondary indexes use more space, so it is advantageous to have a short primary key.

   3. 将index record转换为要插入row：`row_build_w_add_vcol`。

      > 相对应的有将row，转换为index record：`row_build_index_entry`

   ————————————————————————————————————

   **写buffer**

   1. 遍历各个index

   2. 将tuple插入到sortbuffer中`row_merge_buf_add` ;

      通过这个函数可以看出，二级索引中写的是（二级索引的列，主键的值），而不是主键的物理位置。

   3. buffer满了之后（95324）:

      1. 先排序`row_merge_buf_sort`；
      2. 然后将buf写到block中`row_merge_buf_write`；
      3. 将block写回磁盘中`row_merge_write`。
      4. 清空buf：`row_merge_buf_empty`。

### 外部归并排序

*row_merge_sort*

上一节得到merge_file，这里基于这个mergefile进行合并排序。

```c
/** Information about temporary files used in merge sort */
struct merge_file_t {
	int		fd;		/*!< file descriptor */
	ulint		offset;		/*!< file offset (end of file) */
	ib_uint64_t	n_rec;		/*!< number of records in the file */
};
```

这里mergefile的offset是按照sortbufsize为单位（filebuffer即block，虽然申请了 3*sortbufsize的大小，但是每次写还是一个sortbufsize）。

```c
row_merge_write(
/*============*/
	int		fd,	/*!< in: file descriptor */
	ulint		offset,	/*!< in: offset where to write,
				in number of row_merge_block_t elements */
	const void*	buf)	/*!< in: data */
{
	size_t		buf_len = srv_sort_buf_size;
....
	err = os_file_write_int_fd(
		request,
		"(merge)", fd, buf, ofs, buf_len);

```

除了mergefile之外，还有一个tmpfile；用在mergefile的合并排序过程中。

————————————————————————————————————

那么mergefile的合并排序的具体过程如下

1. 首先找到两个偏移，0、half。
2. 按照两个偏移量，对block进行多次merge；每次merge的结果放在tmpfile中，每轮返回的时候进行一个tmpfile与mergefile的置换。
3. 多次merge之后，num_runs=1；那么结束merge。

### 填充新索引

*row_merge_insert_index_tuples*

重复执行下面的过程，将排序好的数据插入到索引中。

1. row_merge_read，从mergefile读取一个block。
2. row_merge_read_rec，从block中读取一个record
3. row_rec_to_index_entry_low，将record转换为一个logical tuple（dtuple_t）
4. BtrBulk::insert，通过BtrBulk工具类，将dtuple插入到新索引中。

### 应用RowLog

*row_log_apply->row_log_apply_ops->row_log_apply_op->row_log_apply_op_low*

```c
/** Index record modification operations during online index creation */
enum row_op {
	/** Insert a record */
	ROW_OP_INSERT = 0x61,
	/** Delete a record */
	ROW_OP_DELETE
};
```

rowlog中的类型有两种，如上：

+ `ROW_OP_DELETE`最终对应着`btr_cur_optimistic_delete`
+ `ROW_OP_INSERT`最终对应着`btr_cur_optimistic_insert`

# 崩溃恢复

> https://dev.mysql.com/doc/refman/5.7/en/innodb-online-ddl-operations.html
>
> If the server exits while creating a secondary index, upon recovery, MySQL drops any partially created indexes. You must re-run the [`ALTER TABLE`](https://dev.mysql.com/doc/refman/5.6/en/alter-table.html) or [`CREATE INDEX`](https://dev.mysql.com/doc/refman/5.6/en/create-index.html) statement.
















