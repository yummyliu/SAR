* TOC
{:toc}
## Write

基于latch coupling，一般对任何更新来说，先对Btree的Root加X lock；而更好的方式，是先对加S lock，即，假设target leaf是safe的，如果不是safe的，再加X lock进行写入；InnoDB中称之为为乐观与悲观的操作，乐观的操作与上述读取操作类似。下面以悲观为例来了解InnoDB中的Btree的更新。

### INSERT的rwlock

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
