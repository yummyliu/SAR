---
layout: post
title: InnoDB源码——INSERT的MTR
date: 2019-05-21 16:35
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - InnoDB
typora-root-url: ../../yummyliu.github.io
---

* TOC
{:toc}
# MTR

## mtr_t结构

几个主要成员的含义：

+ `m_inside_ibuf`：mtr正在操作ibuf

+ `m_memo`：锁记录

+ `m_log`：mtr日志，`m_n_log_recs`：当前有多少日志记录

+ `m_made_dirty`：产生脏页，需要刷盘；

+ `m_modifications`：进行修改，不一定需要刷盘

+ `m_log_mode`：当前mtr的日志模式（是否记录redo与刷脏）

+ `m_user_space`/`m_undo_space`/`m_sys_space`；当前mtr修改的表空间

+ `m_state`:mtr生命周期的四种状态 

+ `m_flush_observer` ： 对于不需要写redo的page操作的刷盘标记(create index)

  ```c
  /** We use FlushObserver to track flushing of non-redo logged pages in bulk
  create index(BtrBulk.cc).Since we disable redo logging during a index build,
  we need to make sure that all dirty pages modifed by the index build are
  flushed to disk before any redo logged operations go to the index. */
  ```

+ `m_mtr` : 当前mtr指针（this）

  > `m_user_space_id`/`m_magic_n` ： 调试代码时才有

## MTR生命周期

```c++
enum mtr_state_t {
	MTR_STATE_INIT = 0,
	MTR_STATE_ACTIVE = 12231,
	MTR_STATE_COMMITTING = 56456,
	MTR_STATE_COMMITTED = 34676
};
```

+ **初始化**：的时候是MTR_STATE_INIT；

+ **启动**：`mtr.start`后是MTR_STATE_ACTIVE；

+ **提交**：`commit`/`commit_checkpoint`：MTR_STATE_COMMITTING
  + log_reserve_and_write_fast：按照buf_free将记录复制到LogBuffer中。
  + add_dirty_page_to_flush_list：将脏页放到flushlist中。
+ **释放资源**：release_resources：在`add_dirty_page_to_flush_list`之后，设置当前状态为commited；

# 乐观的Insert涉及的mtr

在Insert过程中，共有如下5个mtr（如果有索引可能会有额外的mtr），每个mtr中有若干个记录。

+ 分配undo空间
+ 写undo记录
+ 写数据
+ 2pc-prepare
+ 2pc-commit

## 分配undo空间

### trx_undo_assign_undo

+ MLOG_UNDO_HDR_REUSE：insert的undo页在提交的时候就没有用了，只是在insert事务回滚的时候才用上；所以，insert的undo分配每次都是重用之前的cache，只是修改头部数据即可；而update就是需要创建一个undopage header文件块，。

  ```c++
  	if (type == TRX_UNDO_INSERT) {
  		offset = trx_undo_insert_header_reuse(undo_page, trx_id, mtr);
  
  		trx_undo_header_add_space_for_xid(
  			undo_page, undo_page + offset, mtr);
  	} else {
  		ut_a(mach_read_from_2(undo_page + TRX_UNDO_PAGE_HDR
  				      + TRX_UNDO_PAGE_TYPE)
  		     == TRX_UNDO_UPDATE);
  
  		offset = trx_undo_header_create(undo_page, trx_id, mtr);
  
  		trx_undo_header_add_space_for_xid(
  			undo_page, undo_page + offset, mtr);
  	}
  	trx_undo_mem_init_for_reuse(undo, trx_id, xid, offset);
  ```

  修改undopage的头部信息的三个mtr记录

+ MLOG_2BYTES

+ MLOG_2BYTES

+ MLOG_2BYTES

  ![image-20190626135555987](/image/undopage.png)

## 写undo记录

undo的操作就两种，插入和修改；

```c++
/* Operation type flags used in trx_undo_report_row_operation */
#define	TRX_UNDO_INSERT_OP		1
#define	TRX_UNDO_MODIFY_OP		2
```

### trx_undo_report_row_operation

+ MLOG_UNDO_INSERT：一条undo相关redo记录

  ![image-20190626135840069](/image/mlog_undo_insert.png)

> **注意**
>
> 上面两步都是在`trx_undo_report_row_operation`函数中，但是在写入记录之前需要分配undo空间；因此在分配undo空间的时候，单独产生一个新的mtr；然后在`trx_undo_report_row_operation`中，调用trx_undo_page_report_insert；trx_undof_page_add_undo_rec_log写入一个undo相关的redo记录。

## 写数据

首先都是进行乐观的写入，即`mode!=BTR_MODIFY_TREE`。

### row_ins_clust_index_entry_low

+ MLOG_COMP_REC_INSERT：插入一条记录；因为表的存储模式默认是compact，这里是compact insert。

  ![image-20190626140845880](/image/mlog_comp_rec_insert.png)

+ MLOG_FILE_NAME

  checkpoint后对表空间的第一次修改，在mtr.commit的时候，需要写一个MLOG_FILE_NAME记录。所以这个记录可能在任何一个`commit`时出现。

```c++
/** Write MLOG_FILE_NAME records if a persistent tablespace was modified
for the first time since the latest fil_names_clear(). */
inline MY_ATTRIBUTE((warn_unused_result))
bool
fil_names_write_if_was_clean(
```

> 如果数据表上有索引，那么会写二级索引记录：`row_ins_sec_index_entry_low`
> + MLOG_8BYTES
> + MLOG_COMP_REC_INSERT

## 2pc之prepare

### trx_prepare_low

主要是设置Prepare阶段的undo的状态；包括tid与tid状态。

> trx_undo_set_state_at_prepare：1920

+ MLOG_2BYTES
+ MLOG_1BYTE
+ MLOG_4BYTES
+ MLOG_4BYTES
+ MLOG_4BYTES
+ MLOG_WRITE_STRING

## 2pc之commit

### trx_commit

> trx_undo_set_state_at_finish：1874

+ MLOG_2BYTES： 设置事务结束时的undo状态

> trx_sys_update_mysql_binlog_offset:

+ MLOG_4BYTES：设置binlog的位点。

  ```c++
  	mlog_write_ulint(sys_header + field
  			 + TRX_SYS_MYSQL_LOG_OFFSET_LOW,
  			 (ulint)(offset & 0xFFFFFFFFUL),
  			 MLOG_4BYTES, mtr);
  ```

  

# 悲观的Insert涉及的mtr

```c++
# define LIMIT_OPTIMISTIC_INSERT_DEBUG(NREC, CODE)\
if (btr_cur_limit_optimistic_insert_debug > 1\
    && (NREC) >= (ulint)btr_cur_limit_optimistic_insert_debug) {\
        CODE;\
}
```

插入的时候首先尝试乐观的插入——`BTR_MODIFY_LEAF`，当乐观的方式返回DB_FAIL时；进行`BTR_MODIFY_TREE`模式的插入——`btr_cur_pessimistic_insert`。

相比于`btr_cur_optimistic_insert`只需要page上的x-latch，在`btr_cur_pessimistic_insert`函数中，需要tree上的x-latch。具体实施过程中，还是分为三步，但是这里写数据会涉及到树的修改。

+ `btr_cur_ins_lock_and_undo`；检查锁和写undo：与之前乐观的方式类似。

+ `btr_cur_pessimistic_insert`；写数据，在调整数结构之前，为了保证操作一定成功；会在调整之前想表空间中预留足够的空间：`fsp_reserve_free_extents`。

  > NOTE that the operation of this function must always succeed,
  > we cannot reverse it: therefore enough free disk space must be
  > guaranteed to be available before this function is called.
  > @return inserted record */
  > rec_t*
  > btr_root_raise_and_insert(

  预留好空间之后开始写数据

  + 树层加高：btr_root_raise_and_insert
  + 节点分裂：btr_page_split_and_insert

+ mtr提交：提交操作与乐观方式相同，都是在母函数`row_ins_clust_index_entry_low`中提交。

因此这里主要讲一下树结构调整的操作。

## 树层加高

### btr_root_raise_and_insert

#### 申请新page并初始化

MLOG_1BYTE： xdes_set_bit，表空间的Extent Descriptor的修改。

MLOG_4BYTES：frag_n_used ，空闲碎片表中已经用掉的碎片。

MLOG_INIT_FILE_PAGE2

MLOG_4BYTES：fseg_set_nth_frag_page_no，设置碎片的pageno

MLOG_COMP_PAGE_CREATE

MLOG_2BYTES：index level

MLOG_8BYTES：index id

#### 设置新page的前向和后向节点

MLOG_4BYTES：next

MLOG_4BYTES：prev

#### 将原root中的记录复制到新page

> 在innobase/include/rem0rec.ic；列出了record中各个值的偏移
>
> /* We list the byte offsets from the origin of the record, the mask,
> and the shift needed to obtain each bit-field of the record. */

同样是按照偏移来操作page的复制：

+ page_get_infimum_offset：起点

+ page_cur_insert_rec_low：插入

+ page_cur_move_to_next：迭代，直接向后移动；由于page中的record实际上是按照链表的方式组织的，找next节点需要用`rec_get_next_offs`获取偏移，如下图

  ![image-20190627155814848](/../Desktop/btree-delete.png)

#### 复制原root中的lock信息到新page

这里没有mtr记录，只是内存数据的转移

#### 将原root节点清空

MLOG_COMP_PAGE_CREATE：重新创建一个新page

MLOG_COMP_REC_INSERT：只插入node_pointer，就是子节点的key和地址对应关系。

> node_pointer（子节点的key和page地址）
>
> ```c++
> #define	DATA_INT	6	/* integer: can be any size 1 - 8 bytes */
> #define	DATA_SYS_CHILD	7	/* address of the child page in node pointer */
> ```
>

#### 分裂子节点进行插入

树高提升好了后，重新定位插入cursor，然后调用btr_page_split_and_insert，进行插入。

————————————————————————————————————

## 节点分裂

### btr_page_split_and_insert

申请一个新的page，然后将需要分出去的记录转移过去，最后将记录插入到正确的page中。

#### 找到split_rec

节点的分裂位置，无mtr

#### 分配并初始化一个空闲的page

mtr同之前

#### 找到分裂的后半节点的第一个记录

无mtr

#### 尝试将新page添加到树中

btr_attach_half_pages

在btr_attach_half_pages中，调用btr_insert_on_non_leaf_level向non-leaf page中插入一个node_ptr；还是和插入叶子节点类似的逻辑，先btr_cur_search_to_nth_level，然后btr_cur_optimistic_insert；如果乐观的不行，然后悲观的插入。

```c
			btr_cur_search_to_nth_level(
				index, level, tuple, PAGE_CUR_LE,
				BTR_CONT_MODIFY_TREE,
				&cursor, 0, file, line, mtr);
```



无mtr

#### 转移记录

MLOG_COMP_REC_INSERT

MLOG_4BYTES：btr_page_set_prev

MLOG_4BYTES：btr_page_set_next

MLOG_4BYTES

MLOG_COMP_LIST_END_COPY_CREATED：结束复制

MLOG_COMP_LIST_END_DELETE：删除原page中的记录

#### 确定该插入到哪个page

无mtr

#### 重新定位插入cursor

MLOG_COMP_REC_INSERT

MLOG_2BYTES

#### 如果插入失败，重新组织page结构；

n_iterations++

## 其他收尾操作

### 提交事务

trx_commit_low

MLOG_4BYTES：更新binlog位点

### 更新统计信息

MLOG_UNDO_HDR_CREATE

MLOG_COMP_LIST_END_DELETE

MLOG_COMP_REC_CLUST_DELETE_MARK

MLOG_2BYTES

