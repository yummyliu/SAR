---
layout: post
title: InnoDB-RedoLog
date: 2019-06-11 10:51
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - InnoDB
typora-root-url: ../../yummyliu.github.io
---

* TOC
{:toc}
# Philosophy redo 

在InnoDB中，其redo日志就是一种Physiological的日志。其中记录了数据页上的所有变更操作。每个记录的形式如下：

记录类型/表空间/页号

![image-20190613104232099](/image/redorecord.png)

## REDO记录例子

在枚举类型`mlog_id_t`中，共有60多个记录类型。由于每个记录的头部信息都是相同的，根据每个记录类型不同，对应了自己的记录结构的解析方式不同，如下列举了几个记录结构：

- MLOG_nBYTE：向page中写入一个n字节的值

  ![image-20190613105252924](/image/mlog_nbyte.png)

- MLOG_WRITE_STRING：向page中写入一个字符串

  ![image-20190613105351745](/image/mlog_write_string.png)

- MLOG_COMP_REC_INSERT ：写入一个compact类型的数据记录

  ![image-20190613105508567](/image/mlog_comp_rec_insert.png)

## REDO日志管理

> **双一保证**
>
> 默认将`innodb_flush_log_at_trx_commit`设置为1，保证日志Write Ahead；除非系统层面保证了数据不会丢失，如**battery backed raid card**。
>
> 另外还有上层MySQL的`sync_binlog`；也需要设置为1，保证binlog落盘。

为了提高整体的吞吐量，InnoDB采用组提交的方式，从而减少刷盘的次数（LogBuffer）。

> 这里理解就是服务的吞吐量而不是响应时间，因为如果组没有满，就不刷盘的话，是不是就影响了前面事务的响应时间。在PostgreSQL中通过`commit_siblings`配置一个组提交启用的下限，避免系统负载比较低的时候的刷盘等待。

和PostgreSQL的max_wal_size类似，ib_logfile的大小有上限（`innodb_log_file_size * innodb_log_files_in_group < 512GB`），如果设置的过小，为了确保重用redo日志时，被重用的redo日志对应的页已经刷盘；那么可能会频繁的刷脏，这是可以调大redo的整体大小。为了避免checkpoint的刷脏，将pagecleaner和用户线程会按照一些阈值点，进行提前刷脏。因此，业务**update的吞吐量**和**CHECKPOINT回收redo文件**都和这个相关。

因为，我们recovery的时候需要重做这些记录，但是我们并不知道crash的时候相应记录的修改有没有落盘，因此可能会多次执行，所以其中记录保证幂等性([idempotent](http://books.google.com/books?id=S_yHERPRZScC&lpg=PA543&ots=JJtxVQOEAi&dq=idempotent gray&pg=PA543#v=onepage&q=idempotent gray&f=false))。

其实当recovery的时候，和PostgreSQL类似会比较日志的lsn与页面的lsn的大小，只会重做lsn大的日志。

## REDO的总体结构

redo日志按照磁盘扇区大小的**512byte**的块存储，而不是page大小（在percona的xtradb中，可以更改事务记录大小，从而更好的利用SSD等新存储介质），位于`$innodb_log_group_home_dir/ib_logfile`中。

```sql
mysql> show global variables like '%innodb_log_file%';
+---------------------------+----------+
| Variable_name             | Value    |
+---------------------------+----------+
| innodb_log_file_size      | 50331648 |
| innodb_log_files_in_group | 2        |
+---------------------------+----------+
2 rows in set (0.01 sec)
```

逻辑上LogGroup包含了很多Record，但是实际上是分成若干个文件存储(默认是两个)，这些log文件在运行过程中进行循环利用。

Log Group理论上可有很多Group镜像，但是InnoDB中只有一个Group。每个Group中的日志分为若干的文件，每个文件由一个LogFileHeader，每个Block有一个BlockHeader，每个logfile前面有4个Block的大小`#define LOG_FILE_HDR_SIZE	(4 * OS_FILE_LOG_BLOCK_SIZE)`，存储了LogHeader和两个CHECKPOINT Block以及一些其他信息，但是只有在LogGroup的第一个文件中才有CHECKPOINT Block，如下：

![image-20190613102554934](/image/logfileoverview.png)

对于每个BLOCK的头部，同样存储了该Block的信息，比如：

+ flush_bit为日志刷盘的设置的标志，hdr_no为 LSN/*OS_FILE_LOG_BLOCK_SIZE*的结果。

  > It seems that there is no real benefit from the flush bit at all, and even in 5.7 it was completely ignored during the recovery.

+ 当前块的数据字节数

+ 第一个记录的偏离

+ 记录了下一个CHECKPOINT点的位置；在恢复的时候，通过比较CHECKPOINT点和该BLOCK上记录的CHECKPOINT信息，可以得知该块是不是旧的。

![image-20190613102850731](/image/redoblock.png)



那么从整体上来看，InnoDB的redo日志结构如下：

![image-20190613103920894](/image/LogGroupStruct.png)

# REDO的写入流程

1. 数据页的变更必须通过mtr，在`mtr_commit()`中将redo记录写入redo日志；注意这里mtr_commit写入的是redo buffer。具体写入磁盘的时机为：
   1. 事务提交
   2. log buffer的空间使用过半
   3. log CHECKPOINT
2. 一般来说，变更之前会调用`mlog_write_ulint()`函数(或其他类似函数)。
3. 对于一些页面级别的操作，只在记录中记录C函数的编码和参数，解决空间。
   1. 不需要在`trx_undo_header_create() ,trx_undo_insert_header_reuse()`的记录中加参数；
   2. 不能添加不做任何改变的函数，或者需要依赖页外部数据的函数；当前log模块的函数有完备的页面转换，没有足够的理由不要擅自改动。

## 控制结构：log_t

> UNIV_HOTBACKUP
> 在InnoDB中，常见一个UNIV_HOTBACKUP宏；这是之前有一个收费的热备工具——ibbackup相关的。现在基本不用了。
> https://stackoverflow.com/questions/28916804/what-does-univ-hotbackup-mean-in-innodb-source

`log_t*	log_sys`是redo日志系统的关键全局变量。这里基于5.7版代码，逐一了解每个成员变量的含义。这里互斥锁的类型都是：ib_mutex_t；实际上是基于Futex机制实现的FutexMutex（Linux Fast userspace mutex）。

### logbuffer接收redo copy

log_t主要负责三项事情：redo日志写入(拷贝)到缓冲区中；redo缓冲区刷盘；执行CHECKPOINT。因此，其中有两类偏移量：给redo可拷贝写入的buffer的位置（buf_free）、redo缓冲区向磁盘刷盘的位置（buf_next_to_write）。

- `log_flush_order_mutex`：InnoDB中多个bufferPool共享的flush_list上的锁；确保flush_list的顺序访问。

- `byte* buf_ptr`：未对齐的logbuf地址，大小应该是buf_size的两倍；

- `buf_size`：innodb_log_buffer_size配置的buffer大小，但可能会自动扩展。

- `unsigned long buf_free`：buf中空闲的第一个位置

- `max_buf_free`：推荐的buf_free的最大值；

  ```c
  void log_buffer_extend(len) {
  ...
  log_sys->max_buf_free = log_sys->buf_size / LOG_BUF_FLUSH_RATIO - LOG_BUF_FLUSH_MARGIN;
  ...
  }
  ```

  当buf_free超过该值时，可能触发用户线程去写redo；在事务拷redo 到buffer后，也会判断该值，如果超过buf_free，需要刷logbuffer，设置log_sys->`check_flush_or_checkpoint`为true。

- `check_flush_or_checkpoint`：该项为True，表示需要刷logbuffer、或者preflush pool page，或者做CHECKPOINT；

### logbuffer进行刷盘

- `unsigned long buf_next_to_write`：准备flush的redolog的位置，执行完继续推进。
- `volatile bool	is_extending;`：为了避免redo日志过大超过buffer，当redo日志超过buf_size/2时，就会扩展；扩展后，就不会缩减。
- `write_lsn`/`current_flush_lsn`/`flushed_to_disk_lsn`：buf的刷盘分为两步write和sync，write_lsn是最近write的lsn；current_flush_lsn是正在执行write+flush操作的lsn；flushed_to_disk_lsn是已经flush到磁盘的lsn(注意这里是lsn，上面buf中的偏移是ulint)。
- `n_pending_flushes`/`flush_event`：当前等待redo sync的任务，最大值为1；有`mutex`控制对flush_event的互斥访问，从而设置n_pending_flushes。

### checkpoint相关

- `log_group_capacity`：表示当前日志文件的总容量，值为:(Redo log文件总大小 - redo 文件个数 * LOG_FILE_HDR_SIZE) * 0.9，LOG_FILE_HDR_SIZE 为 4*512 字节；超过该容量会重用之前的日志，如果日志对应的page没有刷盘，那么就会丢失数据。

- `max_modified_age_async/max_modified_age_sync/max_checkpoint_age_async/max_checkpoint_age`：进行preflush和CHECKPOINT的一些阈值点。

  ![image-20190529170008180](/image/innodb-page-flush.png)

- `next_checkpoint_no`：每次CHECKPOINT后递增

- `last_checkpoint_lsn/next_checkpoint_lsn`：最近的CHECKPOINT点与当前的CHECKPOINT点；完成之后，last<-next；

- `append_on_checkpoint` ：5.7新增，checkpoint时需要额外记录的redo记录，需要在`mutex`下互斥访问。在做DDL时（例如增删列），会先将包含MLOG_FILE_RENAME2日志记录的buf挂到这个变量上。 在DDL完成后，再清理掉。(log_append_on_checkpoint),主要是防止DDL期间crash产生的数据词典不一致。

  ```c
  /** Set extra data to be written to the redo log during checkpoint.
  @param[in]	buf	data to be appended on checkpoint, or NULL
  @return pointer to previous data to be appended on checkpoint */
  mtr_buf_t*
  log_append_on_checkpoint(
  	mtr_buf_t*	buf)
  {
  	log_mutex_enter();
  	mtr_buf_t*	old = log_sys->append_on_checkpoint;
  	log_sys->append_on_checkpoint = buf;
  	log_mutex_exit();
  	return(old);
  }
  ```

- `n_pending_checkpoint_writes`：大于0时，表示有CHECKPOINT正在进行。如果此时用户发起CHECKPOINT，那么该值+1；结束后该值-1(`log_io_complete`)；

- `checkpoint_buf_ptr/checkpoint_buf`：像日志中写入CHECKPOINT信息的缓冲区

- `checkpoint_lock`：CHECKPOINT信息的缓冲区写入的互斥lock

将log_t上的操作如下图

![image-20190529175154766](/image/innodb-log-t.png)

用户线程执行的mtr结束后，需要将本地的日志copy到logbuffer中，同时将修改的脏页放到flush_list中。

## INSERT流程

理想情况下的插入，不考虑页面分裂，分为4mtr：

1. 分配undo段空间，写undo记录
2. 写undo记录
3. 写数据行
4. 2pc commit

如下图：

![image-20190611161857692](/image/mysql-insert.png)

插入的时候会判断是插入聚集索引还是二级索引，首先考虑聚集索引的插入，即`row_ins_clust_index_entry_low`函数，如下：

![image-20190613134743737](/image/row_ins_clust_index_entry_low.png)

具体树上的插入`btr_cur_optimistic_insert`：

在MySQL的BufferPool中，分为16K大小的frame；Btree上有一个tree cursor(`btr_cur_t`)和page cursor(`page_cur_t`)。在btr_cur_optimistic_insert函数中，基于之前找到的tree cursor进行插入操作，如下：

![image-20190613173809214](/../Desktop/btr_cur_optimistic_insert.png)

具体业内的插入操作`page_cur_insert_rec_low`中，在MySQL的Page中，记录是按照链表的方式组织的，Header中有一个PageDirectory，只是维护了部分记录的位置，因此在每个记录中有一个N_owned字段，用来记录该记录之前**连续有多少没有在PageDirectory中索引记录**。

![image-20190613175716625](/../Desktop/InnoDB-page-directory.png)

在Page中插入会pagedirectory和n_owned等信息。

![image-20190613175907422](/image/page_cur_insert_rec_low.png)

数据写完了，之后开始写redo日志，由`page_cur_insert_rec_write_log`负责。

