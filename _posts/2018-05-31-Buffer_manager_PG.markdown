---
layout: post
title: (译)PostgreSQL的Buffer Manager
date: 2018-05-31 14:36
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - PostgreSQL
typora-copy-images-to: ../image
typora-root-url: ../../yummyliu.github.io
---


缓存管理器管理着共享内存和持久存储之间的数据传输，对于DBMS的性能有很重要的影响；PostgreSQL的Buffer Manager工作的十分高效；

本文介绍了PostgreSQL的缓存管理。第一节介绍一下Buffer Manager的概况，后续的章节分别介绍以下内容：

+ Buffer Manager的架构

+ Buffer Manager的锁

+ Buffer Manager的工作流

+ 缓存环

+ 脏页的刷新

  ![C76949F9-6362-4AA8-A5FB-9537C9A9B970](/image/fig-8-01.png)

### Overview

本节介绍以下帮助后续章节理解的概念

#### Buffer Manager Structure

PostgreSQL的bm（后面buffer manager简称bm）包括一个缓存表，若干缓存描述符，一个缓存池；缓存池存储了数据文件的页，包括数据，索引，以及fm和vm；缓存池是一个数组，数组的下标称为buffer ids；

#### Buffer Tag

```c
/*
 * Buffer tag identifies which disk block the buffer contains.
 *
 * Note: the BufferTag data must be sufficient to determine where to write the
 * block, without reference to pg_class or pg_tablespace entries.  It's
 * possible that the backend flushing the buffer doesn't even believe the
 * relation is visible yet (its xact may have started before the xact that
 * created the rel).  The storage manager must be able to cope anyway.
 *
 * Note: if there's any pad bytes in the struct, INIT_BUFFERTAG will have
 * to be fixed to zero them, since this struct is used as a hash key.
 */
typedef struct buftag
{
    RelFileNode rnode;          /* physical relation identifier */
    ForkNumber  forkNum;
    BlockNumber blockNum;       /* blknum relative to begin of reln */
} BufferTag;

/*
 * Stuff for fork names.
 *
 * The physical storage of a relation consists of one or more forks.
 * The main fork is always created, but in addition to that there can be
 * additional forks for storing various metadata. ForkNumber is used when
 * we need to refer to a specific fork in a relation.
 */
typedef enum ForkNumber
{
    InvalidForkNumber = -1,
    MAIN_FORKNUM = 0,
    FSM_FORKNUM,
    VISIBILITYMAP_FORKNUM,
    INIT_FORKNUM

    /*
     * NOTE: if you add a new fork, change MAX_FORKNUM and possibly
     * FORKNAMECHARS below, and update the forkNames array in
     * src/common/relpath.c
     */
} ForkNumber;

typedef uint32 BlockNumber;
```

在PostgreSQL中，所有的数据文件中的每个页都可以用一个唯一的tag来标识，就是buffer tag；当bm收到一个请求，PostgreSQL会使用这个唯一的tag来处理；

buffer tags包括三个值：如上；其中由于PostgreSQL中一个rel，可能包含多个文件，不同的ForkNumber标识不同类型的文件；

#### How a Backend Process Reads Pages

![](/image/fig-8-02.png)

1. 当读取data或者index的一个page时，PostgreSQL向bm发送一个带有buffer tags的请求
2. bm将buffer id返回给PostgreSQL，如果buffer pool中没有，那么从磁盘load到pool中，然后返回相应的buffer id；
3. backend基于buffer id去读取这个page

#### Page Replacement Algorithm

如果pool满了，那么bm就要进行page的换入换出；关于内存置换策略的研究有很多（OPT，LRU，FIFO，Clock等），PostgreSQL采用的是Clock方式；

#### Flushing Dirty Page

脏页最终会被写出去，PostgreSQL中借助两个辅助进程实现：checkpointer和background writer

> DirectIO:
>
> PostgreSQL中不支持DirectIO，社区中关于为什么不支持DirectIO的讨论如下
>
> https://lwn.net/Articles/580542/
>
> https://www.postgresql.org/message-id/529E267F.4050700@agliodbs.com

### Buffer Manager Structure

![](/image/fig-8-03.png)

PostgreSQL中的bm包括三个部分：

+ buffer pool是一个数组，下表就是buffer_id
+ buffer descripterss也是一个数组，和buffer pool中是一一对应的，保存的是对应page的metadata；
+ buffer table是一个hash table；保存的是buffer tags和buffer descripter id的对应关系；

#### Buffer Table

![](/image/fig-8-04.png)

包括三个部分，如上，并且基于链地址法来解决冲突；data entry中存储的是buffer tag和buffer descripter id的对应关系；

#### Buffer Descripter 

```c
/*
 *  BufferDesc -- shared descriptor/state data for a single shared buffer.
 *
 * Note: Buffer header lock (BM_LOCKED flag) must be held to examine or change
 * the tag, state or wait_backend_pid fields.  In general, buffer header lock
 * is a spinlock which is combined with flags, refcount and usagecount into
 * single atomic variable.  This layout allow us to do some operations in a
 * single atomic operation, without actually acquiring and releasing spinlock;
 * for instance, increase or decrease refcount.  buf_id field never changes
 * after initialization, so does not need locking.  freeNext is protected by
 * the buffer_strategy_lock not buffer header lock.  The LWLock can take care
 * of itself.  The buffer header lock is *not* used to control access to the
 * data in the buffer!
 *
 * It's assumed that nobody changes the state field while buffer header lock
 * is held.  Thus buffer header lock holder can do complex updates of the
 * state variable in single write, simultaneously with lock release (cleaning
 * BM_LOCKED flag).  On the other hand, updating of state without holding
 * buffer header lock is restricted to CAS, which insure that BM_LOCKED flag
 * is not set.  Atomic increment/decrement, OR/AND etc. are not allowed.
 *
 * An exception is that if we have the buffer pinned, its tag can't change
 * underneath us, so we can examine the tag without locking the buffer header.
 * Also, in places we do one-time reads of the flags without bothering to
 * lock the buffer header; this is generally for situations where we don't
 * expect the flag bit being tested to be changing.
 *
 * We can't physically remove items from a disk page if another backend has
 * the buffer pinned.  Hence, a backend may need to wait for all other pins
 * to go away.  This is signaled by storing its own PID into
 * wait_backend_pid and setting flag bit BM_PIN_COUNT_WAITER.  At present,
 * there can be only one such waiter per buffer.
 *
 * We use this same struct for local buffer headers, but the locks are not
 * used and not all of the flag bits are useful either. To avoid unnecessary
 * overhead, manipulations of the state field should be done without actual
 * atomic operations (i.e. only pg_atomic_read_u32() and
 * pg_atomic_unlocked_write_u32()).
 *
 * Be careful to avoid increasing the size of the struct when adding or
 * reordering members.  Keeping it below 64 bytes (the most common CPU
 * cache line size) is fairly important for performance.
 */
typedef struct BufferDesc
{
    BufferTag   tag;            /* ID of page contained in buffer */
    int         buf_id;         /* buffer's index number (from 0) */

    /* state of the tag, containing flags, refcount and usagecount */
    pg_atomic_uint32 state;

    int         wait_backend_pid;   /* backend PID of pin-count waiter */
    int         freeNext;       /* link in freelist chain */

    LWLock      content_lock;   /* to lock access to buffer contents */
} BufferDesc;

```

+ pg_atomic_uint32 state
  + refcount:维护了当前访问这个page的进程数，也叫pin count；有进程访问就++，否则- -；等于0，这个page就叫 unpinned;否则就是pinned；
  + usage_count：维护了这个page被访问了多少次；在页面置换的时候用到；
+ context_lock：页锁
+ freeNext：维护一个freelist

综合， 简单来说descripter的状态就是下面几种：

+ Empty：对应的pool slot没有page

+ Pinned：有进程使用这个page

+ Unpinned：pool slot中有page，但是没有进程使用；

  ![](/image/WX20180530-183451.png)

  > 本文的图中，用不同颜色表示不同的状态

#### Buffer Descriptions Layer

buffer描述符的数组，PostgreSQL启动的时候，该数组都是空的；这些描述符由一个链表构成，就是上面提到的freelist；



![](/image/fig-8-05.png)

> 注意，这里提高的freelist和oracle中的freelist不同；PostgreSQL中的freespace map和oracle中的freelist是类似的；

首先加载第一个page：

1. 从freelist的头部找到一个空的descriptor，而后pin住（refcount ++ ; usage_count++）
2. 在buffer table中，插入一个新的entry（tag ， 上面取到的descripter 的 buffer_id）
3. 将相应的page从磁盘中加载到pool中
4. 把元数据信息存储到descriptor中；
5. 以此类推，。。第二个 第三个

![](/image/fig-8-06.png)

非empty的descriptor不会放回到freelist中，直到发生如下情况：

1. Table 或者 index被drop
2. Database被drop了
3. table或者index被VACUUM FULL清理了

> 为什么用freelist来维护empty descriptor？
>
> 这是一种常规的动态获取内存的策略；[freelist](https://en.wikipedia.org/wiki/Free_list)

buffer descriptors layer还维护了一个uint32类型的数，(nextVictimBuffer： 内存置换的时候用)

#### Buffer Pool

buffer pool就是一个简单的数组，存储了page，每个8KB；

### Buffer Manager Locks

bm使用了多种锁，本节介绍一下这些锁的用途，注意这里提到的锁是指同步机制用到的锁，而不是SQL中的lock。

#### buffer table locks

**BufferMappingLock** 维护了buffer table的数据完整性；这是一个轻量的锁，可以用在share和exclusive模式下：当search的时候使用share，insert/delete的时候使用exclusive；

![](/image/fig-8-07.png)

BufferMappingLock被分成多块，来减少竞争，默认是128块；每一块锁维护一部分对应的hash桶；在图中，是一个典型的例子。buffertable还需要一些其他的锁，比如buffertable删除entry的时候，使用一个spin lock（自旋锁），然而这不是在本文的讨论范围内；

#### Locks for each Buffer Descriptor

每个buffer descriptor使用两个轻量锁：content_lock和io_in_progress_lock，来控制对底层buffer pool slot的访问。当这些值改变的时候，还会用到spinlock；

##### content_lock

读取一个page的时候，content_lock在share的模式；而当以下情形发生的时候，该锁基于exclusive模式：

+ page中插入一个新行，或者改变行的t_xmin和t_xmax值的时候;
+ page中删除一个tuple的时候，或者要收缩free space（vacuum收缩表空间或者热更新）
+ Freezing tuple

##### io_in_progress_lock

当PostgreSQL从磁盘加载数据，或者将page刷到磁盘中的时候，使用这个锁，就是字面意思；

##### spinlock

当一些标志位，比如refcount和usage_count被检查或者更改的时候，使用这个锁，在PostgreSQL9.6中用原子操作替代了这个锁；

#### How the Buffer Manager Works

```c
/*
 * ReadBufferExtended -- returns a buffer containing the requested
 *      block of the requested relation.  If the blknum
 *      requested is P_NEW, extend the relation file and
 *      allocate a new block.  (Caller is responsible for
 *      ensuring that only one backend tries to extend a
 *      relation at the same time!)
 *
 * Returns: the buffer number for the buffer containing
 *      the block read.  The returned buffer has been pinned.
 *      Does not return on error --- elog's instead.
 *
 * Assume when this function is called, that reln has been opened already.
 *
 * In RBM_NORMAL mode, the page is read from disk, and the page header is
 * validated.  An error is thrown if the page header is not valid.  (But
 * note that an all-zero page is considered "valid"; see PageIsVerified().)
 *
 * RBM_ZERO_ON_ERROR is like the normal mode, but if the page header is not
 * valid, the page is zeroed instead of throwing an error. This is intended
 * for non-critical data, where the caller is prepared to repair errors.
 *
 * In RBM_ZERO_AND_LOCK mode, if the page isn't in buffer cache already, it's
 * filled with zeros instead of reading it from disk.  Useful when the caller
 * is going to fill the page from scratch, since this saves I/O and avoids
 * unnecessary failure if the page-on-disk has corrupt page headers.
 * The page is returned locked to ensure that the caller has a chance to
 * initialize the page before it's made visible to others.
 * Caution: do not use this mode to read a page that is beyond the relation's
 * current physical EOF; that is likely to cause problems in md.c when
 * the page is modified and written out. P_NEW is OK, though.
 *
 * RBM_ZERO_AND_CLEANUP_LOCK is the same as RBM_ZERO_AND_LOCK, but acquires
 * a cleanup-strength lock on the page.
 *
 * RBM_NORMAL_NO_LOG mode is treated the same as RBM_NORMAL here.
 *
 * If strategy is not NULL, a nondefault buffer access strategy is used.
 * See buffer/README for details.
 */
Buffer
ReadBufferExtended(Relation reln, ForkNumber forkNum, BlockNumber blockNum,
                   ReadBufferMode mode, BufferAccessStrategy strategy)
{
    bool        hit;
    Buffer      buf;

    /* Open it at the smgr level if not already done */
    RelationOpenSmgr(reln);

    /*
     * Reject attempts to read non-local temporary relations; we would be
     * likely to get wrong data since we have no visibility into the owning
     * session's local buffers.
     */
    if (RELATION_IS_OTHER_TEMP(reln))
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("cannot access temporary tables of other sessions")));

    /*
     * Read the buffer, and update pgstat counters to reflect a cache hit or
     * miss.
     */
    pgstat_count_buffer_read(reln);
    buf = ReadBuffer_common(reln->rd_smgr, reln->rd_rel->relpersistence,
                            forkNum, blockNum, mode, strategy, &hit);
    if (hit)
        pgstat_count_buffer_hit(reln);
    return buf;
}
```

PostgreSQL访问想要的page，通过这个函数，该函数工作基于三个case，如下

##### Accessing a Page Stored in the Buffer Pool

![](/image/fig-8-08.png)

##### Loading a Page from Storage to Empty Slot

![](/image/fig-8-09.png)

##### Loading a Page from Storage to a Victim Buffer Pool Slot

![](/image/fig-8-10.png)

##### Page Eeplacement Algorithm: Clock Sweep

![](/image/fig-8-12.png)

### Ring Buffer

当PostgreSQL读写一个大表的时候，就不会用buffer pool;而是用Ring BUffer，ring buffer是一个小的临时的buffer area。满足下面的条件就会使用到ring buffer，ringbuffer 是在shard memory中分配的（buffer pool不是）：

1. Bulk-reading

   当一个relation大小超过buffer pool的1/4的时候，使用ring buffers，此时的ring buffer size是256KB

2. Bulk-writing：当执行一下命令的时候，使用ring buffer，此时的size是16MB

   + COPY FROM 
   + Create table as
   + Create Materialized view 或者 refresh materialized view
   + alter table

3. vacuum-processing；当一个autovacuum执行的时候，此时的ring buffer size是256KB·

ring buffer使用完立马释放掉，使用ring buffer避免一个大表把整个buffer pool都污染了；

> 为什么默认是256KB?
>
> 对于seq scan，一个256KB的ring，足够小能够放在L2cache中，这样从OS cache向shard buffer cache传输更加高效。小一点也可以，但是也要足够大保证同时传输的多；

### Flushing Dirty Pages

除了换入换出导致的写盘，PostgreSQL还有两个进程来进行读写：checkpointer和bgworker；但是两者的行为不太一样：

checkpointer： 在checkpointing开始的时候，在wal日志中写入一个checkpoint记录，并将脏页刷新；

bgworker：为了减少checkpoint对PostgreSQL性能的影响，bgworker间隔一段时间（默认bgworker_delay:200ms）刷一些page出去（默认bgwriter_lru_maxpages:100pages）；


[interdb-8](http://www.interdb.jp/pg/pgsql08.html)
