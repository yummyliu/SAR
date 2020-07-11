---
layout: post
title: RocksDB Overview(1)——Access Method
date: 2020-06-19 09:58
categories:
  - RocksDB
typora-root-url: ../../layamon.github.io
---
* TOC
{:toc}
之前了解的数据库存储引擎都是以page为基本IO单位的；最近一段时间的工作，主要在RocksDB之上，RocksDB是在LevelDB基础上改造的KV存储引擎，和InnoDB或者说PostgreSQL有很大的不同。抽空将所了解到的整理于此，希望能读到的同学有帮助。

> 其他两篇：
>
> [InnoDB综述](http://liuyangming.tech/07-2019/InnoDB-overview.html)
>
> [PostgreSQL综述](http://liuyangming.tech/07-2018/PostgreSQL-Overview.html)

# 背景

在[from btree to lsm-tree](http://liuyangming.tech/12-2019/leveldb.html)中，我们了解到写B+-tree时，写一条记录就意味着写一个页；在InnoDB中，不考虑bufferpool的合并写和dwbuffer的写放大，如果业务负载随机写N条记录，最坏的情况下，就意味着要写N个页；这带来很大的**写放大**。另外，每个页中都有一些碎片空间，这些无用空间会浪费IO带宽，而针对这个，InnoDB中设计了Compressed Page；Compressed Page同样是要对齐存储（0~4kb对齐到4kb，4~8kb对齐到8kb，8~16kb对齐到16kb），只要对齐就会存在空闲空间。这又带来了**空间放大**。

目前存储介质的演化由HDD到SSD，SSD有更高的iops，但是如果每次**写放大**严重，那么会浪费ssd的写性能；并且SSD相对较贵，如果能够减小空间放大，能够大大节约成本。在SSD之上，LSM-tree得到了应用，LSM-tree相对Btree有较小的写放大，并且存储上更方便压缩；在一些大数据存储系统中，很多都采用了LSM-tree的存储结构。而RocksDB作为独立的KV存储引擎，可以与其他组件组合，同样获得了很多目光。

> **SSD概念**
>
> 这里关于硬件设备，搞明白几个概念：**电气接口**、**存储协议**、**存储介质**。
>
> 当我们有一个主机，现在要接入外接设备，设备要插到主板上，需要该设备能够满足主板上的**电气接口（总线规范）**；插上去后，操作系统上安装相应设备的驱动，驱动中有设备的**存储协议**；之后的IO就封装成相应存储协议的命令，写到外存的**存储介质**上去。电气接口主要有SATA、SAS、PCIe；存储协议有AHCI、SCSI、NVMe；存储介质有NAND、3D-Xpoint。
>
> <img src="/image/rocksdb-overview/ssd-overview.png" alt="image-20200621101610711" style="zoom: 50%;" />

# RocksDB

RocksDb是一个多版本的KV事务型存储引擎（versioned key-value store）；为了对RocksDB有一个整体的认识，这里还是基于《Architecture of DB》中对事务型存储引擎的划分进行讨论。

## Access Method

### Disk Layout

RocksDB对应的文件共用一个FilteNumber，DB创建文件时将FileNumber加上特定的后缀作为文件名，FileNumber在内部是一个uint64_t类型，并且全局递增。不同类型的文件的拓展名不同，例如sstable文件是.sst，wal日志文件是.log。有以下文件类型：（`enum FileType`）：

+ kLogFile，就是Write Ahead Log；

+ kDBLockFile : LOCK，一个rocksdb的数据库同时只能被一个进程打开（[linux file lock](https://gavv.github.io/articles/file-locks/)）；

+ kIdentityFile：IDENTITY，rocksdb实例的唯一标识；

+ kTableFile：SST文件；

+ kDescriptorFile：MANIFEST-***，RocksDB是多版本的KV存储，从一个Version到另一个VERSION的变更叫VersionEdit，这都放在MANIFEST-\*\*\*中。

+ kCurrentFile：CURRENT，最近的一个MANIFEST日志文件

  ![image-20200619150536955](/image/rocksdb-overview/current.png)

+ kInfoLogFile：LOG，LOG.old.***，运行时的错误、统计等信息日志

+ kOptionsFile：配置文件，RocksDB可以从文件中加载配置：OPTIONS-***

> 还有一些不常用的以及不知道干嘛用的...
>
> + kTempFile：临时文件
> + kSocketFile：CONSOLE
> + kMetaDatabase：METADB-

对于SST file，逻辑上分为多层；其中L0是内存的MemTable flush出来构建的，L0中的file按照flush的顺序排序，其中key range大部分都是有重叠的；而非L0的file是每次Compact由上层和本层的若干file合并而来的（level compaction），每层中的file是按照key的大小排序的。

> 为保证LSM-tree的形态合理，确保空间放大系数；打开参数level_compaction_dynamic_level_bytes，从而会基于最底层的大小反向逆推每层的合理大小，从而触发compact来修正LSM-tree。减小空间放大

> checkpoint
>
> RocksDB的某一时间点的物理全量备份，需要提供目标目录，会将需要的文件拷贝过去。

#### SST file layout

首先在RocksDB（或者说任何LSM-tree）中SSTfile是不可变更的，不像Btree中的tablespace会inplace update。

RocksDB在BuildTable后，SST的数据就固定了，Compact会将若干个SST转换为一个新的SST。对于BlockBasedTable其[SST文件的格式](https://github.com/facebook/rocksdb/wiki/Rocksdb-BlockBasedTable-Format)如下：

<img src="/image/rocksdb-overview/blockbasedsst.png" alt="image-20200622183213243" style="zoom:33%;" />

数据在文件中是有序的，并切分为不同的data block中；对应地，在index block中有若干个entry，每个entry的key>=对应data block的last key，便于在index block中二分查找，快速定位data block。具体data block的文件内位置通过结构BlockHandle(offset,size)表示。

除index block外，还有一些别的meta block分别对应不同的元数据。

### Read&Write

逻辑上，RocksDB最小的粒度是kv pair（`(userkey,seq,type) => uservalue`）；取决于SST Table的类型，kv的存储方式不同，默认是的BlockBasedTable。KV可逻辑上分成不同的ColumnFamily，这样不同分区可以分别写自己的MemTable和SST Table，但是共享同一个WAL；

> ColumnFamily对应的kv放在CF专属的MEMTable和SSTFile中，而Table与CF的关系维护在`Version`中。
>
> RocksDB保证**跨CF的的原子写**和**一致性读**快照，以及可以分别对不同CF进行挂载、删除与配置。

下图中省去了Index、BloomFilter、Block Cache和Column Family（下称CF）的概念，描述了RocksDB的读写路径：

![image-20200227180548341](/image/2020-0227-rocksdb-overview.png)

在RocksDB中，写事务提交时只需要保证Log Record落盘即可，Data只需要复制到Active MemTable中，后续的数据落盘由FLush/Compact操作进行。

相比于B+-tree在写放大和空间放大上的不足，LSM-tree能够通过合并随机写减少写放大，以及通过高效的压缩减少空间放大（对每一层可以选用不同的压缩算法）。如下表，进行了简单的对比：

|            | InnoDB                               | RocksDB                                  |
| ---------- | ------------------------------------ | ---------------------------------------- |
| 点查       | 从上到下按照某个分支进行定位。       | 逐层查找，可以基于bloom filter进行过滤。 |
| range scan | 直接按照leafnode的next指针进行遍历。 | 需要合并不同层的数据。                   |
| delete     | 标记删除                             | 标记删除，tombstone；singledelete优化。  |

目前RocksDB提供了基本的KV操作，对应的Key中保存了操作类型：

```cpp
// <user key, sequence number, and entry type> tuple.
struct FullKey {
  Slice user_key;
  SequenceNumber sequence;
  EntryType type;
  
  ...
}
// User-oriented representation of internal key types.
enum EntryType {
  kEntryPut,
  kEntryDelete,
  kEntrySingleDelete,
  kEntryMerge,
  kEntryRangeDeletion,
  kEntryValueIndex,
  kEntryMergeIndex,
  kEntryOther,
};
```

每种操作的接口带有相应的Option；除了基本KV操作外，RocksDB提供了了一些额外操作，关键的有：Merge和Compact。

#### Iterator

https://github.com/facebook/rocksdb/wiki/Iterator-Implementation

##### Fractional cascading

预先将LSM-tree中的sst file的key range关系，保存在FileIndexer中。Version-Get()的时候来读。





## Q&A



