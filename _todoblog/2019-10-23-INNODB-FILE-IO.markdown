---
layout: post
title: PostgreSQL的Buffer只能
date: 2019-10-23 17:30
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---
> * TOC
{:toc}

# 关于buffersize的配置

以下是两个数据库在文档上关于数据缓存区的推荐配置的不同点

+ MySQL/InnoDB:

  > 默认128MB；
  >
  > A larger buffer pool requires less disk I/O to access the same table data more than once. On a dedicated database server, you might set the buffer pool size to 80% of the machine's physical memory size. Be aware of the following potential issues when configuring buffer pool size, and be prepared to scale back the size of the buffer pool if necessary.

+ PostgreSQL

  > 默认128MB；
  >
  > If you have a dedicated database server with 1GB or more of RAM, a reasonable starting value for `shared_buffers` is 25% of the memory in your system. There are some workloads where even larger settings for `shared_buffers` are effective, but because PostgreSQL also relies on the operating system cache, it is unlikely that an allocation of more than 40% of RAM to `shared_buffers` will work better than a smaller amount.

  那么为什么一个大一个小呢？PostgreSQL是多进程的结构，可以共享Kernel Buffer和共享缓存；MySQL是多线程的结构，可以共享进程内的地址空间。

PostgreSQL 的xlog.c中：

Optimize writes by bypassing kernel cache with O_DIRECT when using
   \* O_SYNC/O_FSYNC and O_DSYNC.  But only if archiving and streaming are
   \* disabled, otherwise the archive command or walsender process will read
   \* the WAL soon after writing it, which is guaranteed to cause a physical
   \* read if we bypassed the kernel cache. 

wal_sync_method

# Linux File IO

![[Data flow diagram]](/image/1030-jm-data-flow.png)

## Systerm IO

| Operation | Function(s)                                       |
| --------- | ------------------------------------------------- |
| Open      | `open()`, `creat()`                               |
| Write     | `write()`, `aio_write()`, `pwrite()`, `pwritev()` |
| Sync      | `fsync()`, `sync()`,`fdatasync()`                 |
| Close     | `close()`                                         |

write：只保证数据从应用地址空间拷贝到内核地址空间，即kernel space buffer，通常叫page cache。

只有fsync才保证将数据和元数据都实实在在地落盘了（fdatasync只同步数据部分）。

注意，当open的时候如果加上了O_SYNC参数，那么write调用就等价于write+fsync；

当open加上O_DIRECT参数时，write的时候会绕过kernel buffer，但是需要要求写的时候要对齐写，比如对齐512或者4k；

因此，只有open的时候O_DIRECT|O_SYNC，那么写的时候才是真的物理写。

## Stream IO

| Operation | Function(s)                                                  |
| --------- | ------------------------------------------------------------ |
| Open      | `fopen()`, `fdopen()`, `freopen()`                           |
| Write     | `fwrite()`, `fputc()`, `fputs()`, `putc()`, `putchar()`, `puts()` |
| Sync      | `fflush()`, followed by `fsync()` or `sync()`                |
| Close     | `fclose()`                                                   |

## Memory mapped

| Operation | Function(s)                                                  |
| --------- | ------------------------------------------------------------ |
| Open      | `open()`, `creat()`                                          |
| Map       | `mmap()`                                                     |
| Write     | `memcpy()`, `memmove()`, `read()`, or any other routine that writes to application memory |
| Sync      | `msync()`                                                    |
| Unmap     | `munmap()`                                                   |
| Close     | `close()`                                                    |



原来以为O_DIRECT就直接落盘了，现在发现自己理解错了，这个参数只是绕过了page cache而已；想要写的时候直接落盘，是和参数 O_SYNC 和 O_DSYNC有关（这两个参数的write，可以看做write+fsync），我又看了些文档里对这两个参数的区分解释；分别对应了 file integrity和data integrity；

> To understand the difference between the two types of completion, consider two pieces of file metadata: the file last modification timestamp (st_mtime) and the file length. All write operations will update the last file modification timestamp, but only writes that add data to the end of the file will change the file length. The last modification timestamp is not needed to ensure that a read completes successfully, but the file length is. Thus, O_DSYNC would only guarantee to flush updates to the file length metadata (whereas O_SYNC would also always flush the last modification timestamp metadata).



## 概述

```c++
#include <fstream>
#include <cstring>
#include <iostream>
using namespace std;

struct Person
{
	char name[50];
	int age;
	char phone[24];
	double high;
};

int main () {
	Person mem;
	ifstream infile;
	infile.open("log.dat", ios::binary|ios::in);
	infile.read((char*)&mem, sizeof(Person));
	infile.close();
  
  mem.age++;
	std::cout << mem.age  << ' ';
	std::cout << mem.name << ' ';
	std::cout << mem.phone<< ' ';
	std::cout << mem.high<< std::endl;

	ofstream outfile;
	outfile.open("log.dat", ios::binary|ios::out);
	outfile.write((char*)&mem, sizeof(mem));
	outfile.close();


	return 0;
}
```



![img](/image/linuxio.png)

## Linux AIO 与 directio

> [http://nginx.org/en/docs/http/ngx_http_core_module.html#aio](http://nginx.org/en/docs/http/ngx_http_core_module.html#aio)
>
> When both AIO and [sendfile](http://nginx.org/en/docs/http/ngx_http_core_module.html#sendfile) are enabled on Linux, AIO is used for files that are larger than or equal to the size specified in the [directio](http://nginx.org/en/docs/http/ngx_http_core_module.html#directio) directive, while [sendfile](http://nginx.org/en/docs/http/ngx_http_core_module.html#sendfile) is used for files of smaller sizes or when [directio](http://nginx.org/en/docs/http/ngx_http_core_module.html#directio) is disabled.

## zero-copy

### sendfile()

（Java NIO的transferTo）：不需要内核态和用户态之间的数据拷贝，但是DMA需要在内核中需要维护一个连续的buffer用来传输数据。

![img](/image/sendfile.png)

如果硬件上支持*scatter-n-gather*，那么可以省去这个buffer。

![img](/image/scater.png)

## memory-map

### mmap()/munmap()

![img](/image/mmap.png)

（Java NIO的MappedByteBuffer）上面的zero-copy中，用户态进程只能等待IO完成。如果期间希望操作数据，可以使用mmap。mmap将外存的文件块映射到内存中，虽然又带来了4次上下文切换，但是可以利用OS的页面管理（虚拟空间映射，页面缓存与自动刷出，页面对齐等）。同样会带来占用进程页表和TLB缓存的代价。































































考虑到文件系统如果支持sparsefile，那么文件的物理大小和逻辑大小是不同的。ll 显示的是逻辑大小，du显示的是按block（默认是1024byte）为单位的物理大小。因此有如下情况，du显示是4（就是4*1024，4k），ll显示的是1025（就是逻辑上的大小）

```bash
# dd if=/dev/zero of=test bs=1 count=1025
1025+0 records in
1025+0 records out
1025 bytes (1.0 kB) copied, 0.00209302 s, 490 kB/s
[12:29:04] nestdb-dev /data/mydata/var
# ll
-rw-r--r-- 1 root root       1025 Aug 13 12:29 test
[12:29:16] nestdb-dev /data/mydata/var
# du test
4       test
```

通过fallocate，可以在文件中打洞，如下，打洞后，du还是按block为单位显示实际占用大小，ls还是逻辑大小

```bash
# dd if=/dev/zero of=test bs=1 count=4097
4097+0 records in
4097+0 records out
4097 bytes (4.1 kB) copied, 0.00877725 s, 467 kB/s

# ls -l
-rw-r--r-- 1 root root       4097 Aug 13 12:31 test

# du test
8       test

# fallocate -n -p -o 4095 -l 5 test

# du test
4       test

# ls -l
-rw-r--r-- 1 root root       4097 Aug 13 12:31 test
```



# PostgreSQL和MySQL的相关配置

InnoDB：

+ `innodb_flush_method`：data和log的刷盘配置



PostgreSQL：

+ `wal_sync_method`：
+ `fsync`：





# 参考

[zhenghe](https://zhenghe.gitbook.io/open-courses/ucb-cs162/topic-ensuring-data-reaches-disk)

[linux open](https://linux.die.net/man/2/open)

[postgreSQL double buffer](https://www.postgresql-archive.org/a-question-about-Direct-I-O-and-double-buffering-td2062191.html)