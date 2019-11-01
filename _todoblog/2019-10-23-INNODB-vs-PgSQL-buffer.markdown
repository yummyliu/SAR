---
layout: post
title: PgSQL和MySQL数据缓冲池配置的探讨
date: 2019-10-23 17:30
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - Linux
  - PostgreSQL
  - MySQL
typora-root-url: ../../yummyliu.github.io
---
* TOC
{:toc}
以下是PgSQL和MySQL两个数据库在文档上关于数据缓存区的推荐配置介绍：

+ MySQL/InnoDB:

  > 默认128MB；
  >
  > A larger buffer pool requires less disk I/O to access the same table data more than once. On a dedicated database server, you might set the buffer pool size to 80% of the machine's physical memory size. 

+ PostgreSQL

  > 默认128MB；
  >
  > If you have a dedicated database server with 1GB or more of RAM, a reasonable starting value for `shared_buffers` is 25% of the memory in your system. There are some workloads where even larger settings for `shared_buffers` are effective, but because PostgreSQL also relies on the operating system cache, it is unlikely that an allocation of more than 40% of RAM to `shared_buffers` will work better than a smaller amount.

总结就是，PostgreSQL推荐设置是25%~40%；MySQL推荐设置是80%；那么是什么造成这两个的不同呢？本文按照个人理解，从Linux的文件IO说起。

# Linux File IO

我们要将数据库的数据写入到磁盘文件中，要经历图中的几层缓存；

![image-20191101105539630](/image/1030-jm-data-flow.png)

首先是自己维护的BufferPool，然后在我们调用文件读写API时，在Library内部也会有缓存；这部分缓存也是在用户态；

然后，当我们调用write将数据写出时，首先会写入到kernel Buffer中（前提是没有打开O_DIRECT，后面会详细讨论）。

最后，操作系统会周期性的清理自己的Buffer就是将Kernel Buffer中的数据写到磁盘中，或者我们对文件调用fsync，那么才最终落盘。

> 通过DMA的机制写盘，不占用系统CPU资源。

了解了上述大致过程，那么还是不了解什么造成的MySQL和PostgreSQL的不同，下面我们详细讨论一下文件读写的API。

## 文件读写API

在Linux下编程，我们可以有很多种方式操作文件，下面对各个接口进行了梳理：

+ 系统调用

  | Operation | Function(s)                                       |
  | --------- | ------------------------------------------------- |
  | Open      | `open()`, `creat()`                               |
  | Write     | `write()`, `aio_write()`, `pwrite()`, `pwritev()` |
  | Sync      | `fsync()`, `sync()`,`fdatasync()`                 |
  | Close     | `close()`                                         |

  这部分是OS文件系统的调用，会陷入内核态；其中write：只保证数据从应用地址空间拷贝到内核地址空间，即kernel space buffer，只有fsync才保证将数据和元数据都实实在在地落盘了（fdatasync只同步数据部分，这里涉及到file integrity和data integrity，可以参考Linux的手册）。

  > 注意，当open的时候如果加上了O_SYNC参数，那么write调用就等价于write+fsync；当open加上O_DIRECT参数时，**write的时候会绕过kernel buffer**，但是需要要求写的时候要对齐写，比如对齐512或者4k；

  因此，只有open的时候O_DIRECT|O_SYNC，那么写的时候才是真的物理写。

+ glibc

  | Operation | Function(s)                                                  |
  | --------- | ------------------------------------------------------------ |
  | Open      | `fopen()`, `fdopen()`, `freopen()`                           |
  | Write     | `fwrite()`, `fputc()`, `fputs()`, `putc()`, `putchar()`, `puts()` |
  | Sync      | `fflush()`                                                   |
  | Close     | `fclose()`                                                   |

  这部分是C library的IO流式读写，对底层系统调用进行了封装；如果采用这种方式操作文件，那么fwrite的时候，可能数据还是留在用户态，这优化了频繁陷入内核态的上下文切换的代价。

+ mmap

  | Operation | Function(s)                       |
  | --------- | --------------------------------- |
  | Open      | `open()`, `creat()`               |
  | Map       | `mmap()`                          |
  | Write     | `memcpy()`, `memmove()`, `read()` |
  | Sync      | `msync()`                         |
  | Unmap     | `munmap()`                        |
  | Close     | `close()`                         |

  mmap将外存的文件块映射到内存中，可以利用OS的页面管理（虚拟空间映射，页面缓存与自动刷出，页面对齐等），**一些轻量级的存储引擎直接利用mmap，而不是自己实现缓冲区管理，比如BlotDB**；注意mmap同样会占用进程页表，进而可能有TLB miss的代价。

+ Zero-copy

  | Operation  | Function(s)  |
  | ---------- | ------------ |
  | Transferto | `sendfile()` |

  上述文件读写方式，适用于对单个文件的多次频繁IO；而当我们需要将一个大文件传输到另一个大文件中时，如果采用read+write的方式，那么会和下图类似，频繁的在用户态和内核态之间拷贝数据。

  ![image-20191101105156152](/image/1030-read-write.png)

  这时可以考虑使用sendfile。sendfile不需要内核态和用户态之间的数据拷贝，但是DMA需要在内核中需要维护一个连续的buffer用来传输数据。

  ![image-20191101105456535](/image/1030-sendfile.png)

+ Linux AIO

  AIO需要文件按照**O_DIRECT**的方式打开，AIO的接口有两种：

  + Linux本身的系统调用： **[io_submit](../man2/io_submit.2.html)**(2),**[io_setup](../man2/io_setup.2.html)**(2), **[io_cancel](../man2/io_cancel.2.html)**(2), **[io_destroy](../man2/io_destroy.2.html)**(2), **[io_getevents](../man2/io_getevents.2.html)**(2)),
  + POSIX类型的glibc接口：**[aio_cancel](../man3/aio_cancel.3.html)**(3),**[aio_error](../man3/aio_error.3.html)**(3), **[aio_init](../man3/aio_init.3.html)**(3), **[aio_read](../man3/aio_read.3.html)**(3), **[aio_return](../man3/aio_return.3.html)**(3), **[aio_write](../man3/aio_write.3.html)**(3), **[lio_listio](../man3/lio_listio.3.html)**

  具体的使用方式，本文暂不讨论。

  > 或者可以只用非阻塞的方式（O_NONBLOCK）打开文件，然后通过epoll进行多路复用读写

# 讨论PgSQL和MySQL缓冲区配置

那么回到一开始的问题，为什么两者设置的不同呢？（以下只是个人分析，考虑不周，请指正）。

数据库要保证数据的写入完全在自己的掌控之中，不管是写入的时间还是写入的位置。BufferPool在DB中，可以作为读cache和写buffer，减少物理读的次数，也减少物理写的次数。但是，最终BufferPool中的数据还是要落盘的，那么落盘时的操作就是调用上述的API，并且可以通过参数的配置决定写入的方式；

两者的区别在于是否要留一大部分Kernel Buffer；在MySQL/InnoDB中，我们可以通过参数`innodb_flush_method`，决定data和log是否使用O_DIRECT（绕过Kernel Buffer）和O_SYNC；而在PostgreSQL中，我们只有通过`wal_sync_method`和`fsync`选择是否O_SYNC，没有选择O_DIRECT的入口。

啰嗦了这么多，我认为造成这种差异的原因就是多进程架构和多线程架构的区别。PostgreSQL是多进程的架构，那么进程间能共享的除了shared_memory外，就是Kernel Buffer了；但是shared_memory不能配置的太大，那样的话每个PostgreSQL进程的页表就会很庞大，进而带来TLB的miss等问题（虽然可以通过huge_page来启用大页，进而减小页表的大小，huge_page不是所有系统都支持的）；因此，shared_memory的推荐配置就是25%到40%，而不是所有的内存，因为Kernel Buffer对于PostgreSQL也很重要，在PostgreSQL 的xlog.c中，可以看到这样一句话：

> Optimize writes by bypassing kernel cache with O_DIRECT when using
> O_SYNC/O_FSYNC and O_DSYNC.  But only if archiving and streaming are
> disabled, otherwise the archive command or walsender process will read
> the WAL soon after writing it, which is guaranteed to cause a physical
> read if we bypassed the kernel cache.

可以看出PostgreSQL的多进程设计对Kernel Buffer是有依赖的，因此在线上监控的时候，Kernel Buffer是否一直充满，也是一个很有必要的监控项；当Kernel Buffer由于某些操作被刷出，会引起PostgreSQL的性能波动。

而MySQL是多线程架构，bufferPool就是进程堆内空间，而且可以有多个bufferpool，那么可以尽量多将内存留给自己用，这样就会将更多的热点数据放在内存中处理，得到很好的性能。

# 参考

[Ensuring data reaches disk](https://lwn.net/Articles/457667/)

[zhenghe](https://zhenghe.gitbook.io/open-courses/ucb-cs162/topic-ensuring-data-reaches-disk)

[linux open](https://linux.die.net/man/2/open)

[postgreSQL double buffer](https://www.postgresql-archive.org/a-question-about-Direct-I-O-and-double-buffering-td2062191.html)

[linux aio](https://github.com/littledan/linux-aio)