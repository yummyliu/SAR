---
layout: post
title: 
date: 2020-07-09 21:00
categories:
  -
typora-root-url: ../../layamon.github.io
---
> * TOC
{:toc}

# Buffer Manager

[rocksdb mmap vs directio](https://github.com/facebook/rocksdb/wiki/Direct-IO)

RocksDB implements all the alignment logic inside `FileReader/FileWriter`, one layer higher abstraction on top of File classes to make the alignment ignorant to the OS.

## write-buffer

#### IO

##### options

RocksDB的文件都是append的，通过配置参数`bytes_per_sync`(SST)和`wal_bytes_per_sync`（WAL)，使得在写一定数据量脏数据后，调用`sync_file_range`进行文件部分sync。

还可配置 `rate_limiter`，控制刷盘速度，避免影响线上查询。

如果使用DIO或者文件系统没有page cache，打开`writable_file_max_buffer_size`，这是一个rocksdb自己的文件缓存区。

## cache

### Block Cache

其中的block是非压缩的（可通过block_cache_compressed指定专门的压缩cache）。

> The compressed block cache can be a replacement of OS page cache, if [Direct-IO](https://github.com/facebook/rocksdb/wiki/Direct-IO) is used.

block的cache置换算法有两种：LRU和Clock；都通过shared的方式减少锁竞争。

### Row Cache

### table cache

