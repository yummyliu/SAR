---
layout: post
title: InnoDB的Btree与rwlock互动解析
date: 2019-07-08 16:35
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - InnoDB
typora-root-url: ../../yummyliu.github.io
---
* TOC
{:toc}
# 隐式线程锁

线程锁的对象是程序运行过程中bufferPool中的page，或其他cache中的对象；有两种类型：mutex和rw_lock。

## mutex

基于系统提供的原子操作，用在内存共享结构的串行访问上，主要有如下一些mutex。

+ Dictionary mutex（Dictionary header)
+ Transaction undo mutex，Transaction system header的并发访问，在修改indexpage前，在Transaction system的header中写入一个undo log entry。
+ Rollback segment mutex，Rollback segment header的并发访问，当需要在回滚段中添加一个新的undopage时，需要申请这个mutex。
+ lock_sys_wait_mutex：lock timeout data
+ lock_sys_mutex：lock_sys_t
+ trx_sys_mutex：trx_sys_t
+ Thread mutex：后台线程调度的mutex
+ query_thr_mutex：保护查询线程的更改
+ trx_mutex：trx_t

等等

## rw_lock

rw_lockInnoDB实现的自旋锁；多个readthread可以持有一个s模式的rw_lock。但是，x模式的rw_lock只能被一个writethread持有；

> `lock_word`
>
> 为了避免writethread被多个readthread饿死，writethread可以通过排队的方式阻塞新的readthread，每排队一个writethread将lockword减X_LOCK_DECR（新的SX锁等待时，减X_LOCK_HALF_DECR）。在wl6363中，标明了加了SX锁后lock_word不同取值的意思；其中lock_word=0表示加了xlock；lock_word= 0x20000000没有加锁；

在5.7中，rw_lock共有四种类型，（在5.7新加了一个[SX](https://dev.mysql.com/worklog/task/?id=6363)类型）。

```c
enum rw_lock_type_t {
	RW_S_LATCH = 1,
	RW_X_LATCH = 2,
	RW_SX_LATCH = 4,
	RW_NO_LATCH = 8
};
/*
 LOCK COMPATIBILITY MATRIX
    S SX  X
 S  +  +  -
 SX +  -  -
 X  -  -  -
 */
```

这个新加的SX锁，从功能上可以由一个S锁加一个X锁代替。但是这样需要额外的原子操作，因此将两个整个为一个SX锁。当持有SX锁时，申请X锁需要'x-lock;sx-unlock;'。当持有X锁时，X锁可也降级为SX锁，而不需要'sx-lock；x-unlock'。

rw_lock可以用在需要并发读写的结构上，比如Btree索引，文件空间管理等，如下。

- Secondary index tree latch ，Secondary index non-leaf 和 leaf的读写
- Clustered index tree latch，Clustered index non-leaf 和 leaf的读写
- Purge system latch，Undo log pages的读写，
- Filespace management latch，file page的读写

而最常见的还是用在Btree操作中，本文详细介绍Btree与rw_lock的互动。

# Btree与rw_lock

在MySQL中主要就是针对Btree的并发访问，其中有两个锁对象

+ dict_cache(dict_index_t)，index元数据

+ Page(buf_pool->page)，节点数据；buffer_pool中的page是通过hash表组织的；加锁的时候首先通过(spaceid,pageno)的hash值，找到buffer pool中的page；然后对该内存的page对象加锁，如下代码

  ```c
  	hash_lock = buf_page_hash_lock_get(buf_pool, page_id);
  loop:
  	block = guess;
  
  	rw_lock_s_lock(hash_lock);
  ```

在Btree扫描过程中，首先在index元数据上加相应模式的rwlock；然后逐层向下遍历，为了避免死锁，当需要获取下一层的锁时，需要先将持有的本层的锁释放；如此找到目标层的node，然后对目标page加锁，进行相应操作。

在对Btree操作时，有如下的基本操作模式。

```c
/** Latching modes for btr_cur_search_to_nth_level(). */
enum btr_latch_mode {
	/** Search a record on a leaf page and S-latch it. */
	BTR_SEARCH_LEAF = RW_S_LATCH,
	/** (Prepare to) modify a record on a leaf page and X-latch it. */
	BTR_MODIFY_LEAF	= RW_X_LATCH,
	/** Obtain no latches. */
	BTR_NO_LATCHES = RW_NO_LATCH,
	/** Start modifying the entire B-tree. */
	BTR_MODIFY_TREE = 33,
	/** Continue modifying the entire B-tree. */
	BTR_CONT_MODIFY_TREE = 34,
	/** Search the previous record. */
	BTR_SEARCH_PREV = 35,
	/** Modify the previous record. */
	BTR_MODIFY_PREV = 36,
	/** Start searching the entire B-tree. */
	BTR_SEARCH_TREE = 37,
	/** Continue searching the entire B-tree. */
	BTR_CONT_SEARCH_TREE = 38
};
```

这些操作模式作为btr_cur_search_to_nth_level的参数latch_mode（无符号32位整型）的低字节；而高字节放一些标记位，用来判断rwlock的类型，加锁后，返回一个目标层（不一定是叶子层）page的cursor；

对于查询操作，之后MySQL层通过cursor的get_next不断调用`row_search_mvcc`读取一行，然后将cursor保存起来，下次再读取时，再restore出来使用。

这里只讨论Btree的操作，InnoDB的Btree的任何读写操作，首先需要定位Btree的位置（`btr_cur_search_to_nth_level`），返回一个目标leafpage的cursor；

> 基于该cursor，会根据一致性锁定读还是非锁定读，决定创建一个readview还是加意向锁；

查询操作主要用到slock，不会改动Btree；而insert/delete/update，会对Btree

产生修改，这里锁的使用就值得讨论了。

## 加锁入口

入口是`btr_cur_search_to_nth_level`，如下是该函数的大体逻辑：

### **1. 初始化扫描指令**；

函数一开始，识别高位的标记得到如下信息，之后后将高位信息抹除(`BTR_LATCH_MODE_WITHOUT_FLAGS`)。

+ btr_op：需要ibuf缓存的操作（`btr_op_t`，btr0cur.c:1117）

+ estimate：是否是在查询优化阶段调用的`btr_cur_search_to_nth_level`。

+ lock_intention：要对Btree进行的修改意图（`btr_intention_t`）。

+ modify_external：在BTR_MODIFY_LEAF模式中，是否需要修改外部存储的数据。

### 2. 查找AHI信息

`btr_search_guess_on_hash`，首先尝试基于AHI查询，成功就返回。

### 3. 解析扫描的指令

+ **加锁模式**：在第一步中，将高位的标记信息已经抹除；这里（btr0cur.cc:959）基于latch_mode和第一步得到的信息，对index加rw_lock；

比如给Index加S锁：`mtr_s_lock(dict_index_get_lock(index), mtr);`

另外，根据对index加锁类型，设置变量`upper_rw_latch`（rw_lock_type_t ），后续给索引块加锁会参考。

+ **查询模式**：根据参数`mode`定义的查询模式 ，决定cursor搜索方式（page_cur_mode_t：与Btree键值的比较大小方式，与Rtree是否相交/包含等位置关系）（1043）。

### 4. (search_loop)递归查找

迭代多次，直到到达指定level（大部分情况level=0，即找到叶子节点；level!=0的一种情况是节点分裂，需要向父节点添加node_ptr），在循环中有如下步骤。

1. 确定`rw_latch`的模型；非特殊情况，第三步的`upper_rw_latch`就是这里的rw_latch类型。

2. `buf_page_get_gen`按照rw_latch类型读取page到buf_pool中，并加锁。

   ```c
   	switch (rw_latch) {
   			case RW_SX_LATCH:
   		rw_lock_sx_lock_inline(&fix_block->lock, 0, file, line);\
   ```

3. 1265，第一次取出的root节点；通过root节点的得到Btree的height；

4. 1440，在取出的page中，根据目标tuple，采用二分法，在page中定位page_cursor（可以是最终的叶子节点的键值对，可以是非叶子节点的node_ptr）；

   ```c
   	/* Perform binary search until the lower and upper limit directory
   	slots come to the distance 1 of each other */
   
   	while (up - low > 1) {
   		mid = (low + up) / 2;
   
   		cmp = cmp_dtuple_rec_with_match(
   			tuple, mid_rec, offsets, &cur_matched_fields);
   	}
   ```

5. 1487，如果不是最终的level；height—;

6. 1780，迭代到该节点的子节点；n_blocks++；在查找过程中维护了一个路径block数组。

7. height!=0，继续迭代search_loop，返回1；height==0（1306），这时根据latch_mode进行遍历过程的收尾。

### 5. 设置cursor

(1862)找到后设置cursor的low_match和up_match等参数；

### 6. 函数退出

因为调用`btr_cur_search_to_nth_level`的调用者可能已经在外面加锁了，那么退出还是对index加s锁 。（由参数has_search_latch判断，该参数只能为0或者`RW_S_LATCH`；）。

## Btree变更操作

### INSERT的rwlock

以悲观Insert为例

**1. 按BTR_MODIFY_TREE模式，定位cursor，并对index和page加锁。**

+ 首先给索引加SX锁（btr_cur_search_to_nth_level：976；）
  + 在查找的过程中，不需要加锁（rw_latch=RW_NO_LATCH）；
  + 最终将找到的leafpage，以及其左右page加X，见`btr_latch_leaves_t`。

```c
	if (height == 0) {
		if (rw_latch == RW_NO_LATCH) {
			latch_leaves = btr_cur_latch_leaves(
				block, page_id, page_size, latch_mode,
				cursor, mtr);
		}
```

**2. 执行btree悲观插入**

此时index->lock->lock_word = 0x10000000了；即已经在上述步骤中加了SXlock。这时，该index不能被其他线程修改，但是可以读。

然后在pessimistic insert中，通过`btr_page_split_and_insert`修改btr_cur_search_to_nth_level中定位的cursor的page；

在修改叶子节点的时候需要在上层添加一个node_ptr(`btr_insert_on_non_leaf_level`)；这里接着调用btr_cur_search_to_nth_level找到指定的父亲分支节点（这里的latch_mode就是BTR_CONT_MODIFY_TREE，如下），但是level就不是0了，因为要找分支节点。

```c
			btr_cur_search_to_nth_level(
				index, level, tuple, PAGE_CUR_LE,
				BTR_CONT_MODIFY_TREE,
				&cursor, 0, file, line, mtr);
```

最终找到指定分支block的时候，对找到的block(level!=0)加X锁；

```c
			if (latch_mode == BTR_CONT_MODIFY_TREE) {
				child_block = btr_block_get(
					page_id, page_size, RW_X_LATCH,
					index, mtr);
```

然后乐观或者悲观的插入；继续重复这个过程。

因此，总结步骤如下：

1. 在插入的时候首先通过btr_cur_search_to_nth_level在整个index上加SX锁，然后进行定位，并对找到的page与邻居加锁；
2. 调用btr_cur_pessimistic_insert，进行分裂；具体的分裂操作，见另一篇文章「InnoDB源码解析——MTR与Btree操作」
3. 分裂的时候如果需要继续分裂，还是通过btr_cur_search_to_nth_level定位并加锁后，重复操作。

### UPDATE的rwlock

更新行的具体逻辑的入口函数是`row_upd_clust_step`。同样是分为乐观更新和悲观更新。

在row_upd_clust_step之前，

1. 先调用`btr_cur_search_to_nth_level`定位了要更新的cursor位置；此时只是查找，索引加S锁。然后将cursor暂存。

2. 恢复cursor`	success = btr_pcur_restore_position(mode, pcur, &mtr);`，在恢复cursor时给对应的page加X锁`btr_cur_optimistic_latch_leaves`；加锁位置在buf0buf.cc:4720。
3. 执行如下代码进行更新，首先尝试乐观更新；更新的时候，如果新元组的大小和原来相同，那么就写完undo日志`trx_undo_report_row_operation`后，直接原地更新： `btr_cur_update_in_place->row_upd_rec_in_place`。

在update的时候，如果更新的列是有序的，那么需要标记删除+插入，见如下代码(`row_upd_clust_rec`)，这里存在一个**Halloween problem**问题（通过undo构建版本解决）。

### DELETE的rwlock

在InnoDB中，上层的Delete和Update最终都是调用的`row_update_for_mysql(((byte*) record, m_prebuilt);)`，只不过是参数内容不同`m_prebuilt->upd_node->is_delete = TRUE;`。

在InnoDB中发起delete，只是在要删除的元组上标记删除，相当于是一次更新操作；

而最后发起删除的是purge线程；

Purge线程类似PostgreSQL的Vacuum，会清理update/delete中标记删除的数据。产生标记删除的事务放在一个history_list（就是purge的队列）中，由参数`innodb_max_purge_lag`控制大小。

Purge线程在发起删除的时候，不管是清理一级索引还是二级索引，都是先尝试乐观删除：

+ 一级索引：

  `row_purge_remove_clust_if_poss_low(BTR_MODIFY_LEAF)->btr_cur_optimistic_delete`

+ 二级索引：`row_purge_remove_sec_if_poss_leaf`

然后再悲观删除：

+ 一级索引：

  `row_purge_remove_clust_if_poss_low(BTR_MODIFY_TREE)->btr_cur_pessimistic_delete`

+ 二级索引：`row_purge_remove_sec_if_poss_tree`

————————————————————————————————————

关于Delete过程的锁，同样是在调用`btr_cur_optimistic_delete`函数之前，通过search_level加锁，

+ 在调用`btr_pcur_restore_position_func`读取cursor时，对page进行加X锁（`btr_cur_optimistic_latch_leaves`）。

+ 而在整个索引上加SX。

————————————————————————————————————

在删除过程中，对Btree的操作有两种：btr_lift_page_up、btr_compress。

+ btr_compress：如果达到merge_threshold的话，将该block和相邻的block合并；

+ btr_lift_page_up：如果没有左右相邻的page，如下判断，那么将该叶子节点的记录提升到father节点中。

```c
	if (left_page_no == FIL_NULL && right_page_no == FIL_NULL) {
		/* The page is the only one on the level, lift the records
		to the father */

		merge_block = btr_lift_page_up(index, block, mtr);
		goto func_exit;
	}
```

具体的合并操作，见另一篇文章「InnoDB源码解析——MTR与Btree操作」

# 总结

查询扫描前，在索引树上加`btr_search_s_lock`；找到之后释放

插入时，需要RW_X_LATCH对应的leafpage。如果需要叶子分裂，首先在整个index上加X。然后再三个leafpage（prev、current、next）上加X，为了避免死锁，都是先获取左page的lock，之后再持有下一个page的锁。

# 参考

[mysql-rr](https://blog.pythian.com/understanding-mysql-isolation-levels-repeatable-read/)

[mysql-index-lock](http://mysql.taobao.org/monthly/2015/07/05/)

[尝试理解lock](https://github.com/octachrome/innodb-locks)