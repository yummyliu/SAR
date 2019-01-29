---
layout: post
title: /proc/pid里的东西
date: 2018-05-30 11:31
header-img: "img/head.jpg"
categories: jekyll update
tags:
   - Linux
---

* TOC
{:toc}
在Linux

```bash
[postgres@ymtest 94103]$ ps aux  |grep 94103 | grep -v grep
postgres  94103  0.0  0.0 38795672 2500 ?       Ss   Jan18   0:22 postgres: autovacuum launcher process
[postgres@ymtest 94103]$ ll
total 0
dr-xr-xr-x 2 postgres postgres 0 Jan 29 10:59 attr
-rw-r--r-- 1 postgres postgres 0 Jan 29 10:59 autogroup
-r-------- 1 postgres postgres 0 Jan 29 10:59 auxv
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 cgroup
--w------- 1 postgres postgres 0 Jan 29 10:59 clear_refs
-r--r--r-- 1 postgres postgres 0 Jan 18 16:25 cmdline
-rw-r--r-- 1 postgres postgres 0 Jan 29 10:59 comm
-rw-r--r-- 1 postgres postgres 0 Jan 29 10:59 coredump_filter
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 cpuset
lrwxrwxrwx 1 postgres postgres 0 Jan 29 10:59 cwd -> /export/postgresql/affdata/affdata
-r-------- 1 postgres postgres 0 Jan 29 10:59 environ
lrwxrwxrwx 1 postgres postgres 0 Jan 29 10:59 exe -> /usr/local/pgsql/bin/postgres
dr-x------ 2 postgres postgres 0 Jan 29 10:59 fd
dr-x------ 2 postgres postgres 0 Jan 29 10:59 fdinfo
-r-------- 1 postgres postgres 0 Jan 29 10:59 io
-rw------- 1 postgres postgres 0 Jan 29 10:59 limits
-rw-r--r-- 1 postgres postgres 0 Jan 29 10:59 loginuid
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 maps
-rw------- 1 postgres postgres 0 Jan 29 10:59 mem
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 mountinfo
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 mounts
-r-------- 1 postgres postgres 0 Jan 29 10:59 mountstats
dr-xr-xr-x 5 postgres postgres 0 Jan 29 10:59 net
dr-x--x--x 2 postgres postgres 0 Jan 29 10:59 ns
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 numa_maps
-rw-r--r-- 1 postgres postgres 0 Jan 29 10:59 oom_adj
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 oom_score
-rw-r--r-- 1 postgres postgres 0 Jan 29 10:59 oom_score_adj
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 pagemap
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 personality
lrwxrwxrwx 1 postgres postgres 0 Jan 29 10:59 root -> /
-rw-r--r-- 1 postgres postgres 0 Jan 29 10:59 sched
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 schedstat
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 sessionid
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 smaps
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 stack
-r--r--r-- 1 postgres postgres 0 Jan 18 16:25 stat
-r--r--r-- 1 postgres postgres 0 Jan 18 16:25 statm
-r--r--r-- 1 postgres postgres 0 Jan 21 14:11 status
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 syscall
dr-xr-xr-x 3 postgres postgres 0 Jan 29 10:59 task
-r--r--r-- 1 postgres postgres 0 Jan 29 10:59 wchan
```



特点：

1. 简洁：只有几百个系统调用；
2. 一切都是文件来对待，使得对文件和设备的操作都是相同的接口：open read write lseek close；
3. 内核和各种系统工具都是基于C编写的，各种硬件架构上移植能力突出；
4. Unix进程创建迅速，有一个独特的fork系统调用；
5. 简单稳定的进程间通信原语

Linux的基础是内核，C库，工具集，系统的基本工具；

##### 单内核和微内核

1. 大多数Unix都设计为单内核，所有内核服务都运行在同一个地址空间，简单性能高；
2. 微内核被划分为多个独立的过程，只有请求特权服务的服务器才运行在特权模式先，要么就是运行在用户空间；系统采用进程间通信的机制互换服务；IPC的开销高于函数调用，又会设计

##### Linux与Unix比较

1. linux单内核，不过汲取了微内核的精华：

   - 模块化设计
   - 抢占式内核
   - 支持内核线程
   - 动态装载内核模块

   规避了微内核设计上的性能损失缺陷，所有事情都在内核态，直接调用函数；linux是模块化，多线程，内核本身可调度的系统；

2. linux可以动态加载内核模块

3. linux支持对称多处理（SMP）机制

4. linux支持抢占，允许在内核运行的任务优先执行的能力；

5. linux内核不区分线程和进程；对于内核来说，多有进程都一样——只不过其中一些共享资源；

6. 用户空间的设备文件系统，面向对象的设备模型，热拔插事件；

7. linux体现了自由的精髓，不会被出现一些商业宣传而没有实际作用的功能；

通过VFS，程序可以通过标准的Unix系统调用对不同的文件系统进行读写；

##### Unix文件系统概念：

- 文件
- 目录项：在Unix中，目录属于普通文件，VFS把目录当做文件对待，所以可以对目录执行和文件相同的操作。
- 索引节点（inode）：Unix中，将文件和文件相关信息加以区分，比如访问控制权限，大小，拥有者，创建时间等放在一个单独的数据结构中
- 安装节点（mount point）: 在Unix中，安装点称为一个命名空间，所有安装的文件系统作为根文件系统树的枝叶，与之不同的是Window和DOS使用驱动字母，这将命名空间分为设备和分区的做法，相当于把硬件细节暴露出来；

##### 块IO

系统中能够随机访问固定大小数据片的硬件设备称作块设备，比如硬盘；另一种就是字符设备，按照字节流的方式被有序的访问，像串口和键盘。

内核管理块设备比字符设备细致的多，以至于块IO是一个单独的子模块。由于扇区是设备的最小可寻址单元。块必须是扇区大小的2的整数倍，并且要小于页面大小。通常是512Byte，1KB，4KB；

###### 块IO操作轻量级容器：bio结构体

该结构体代表了活动的以片段链表形式组织的块IO操作。这样不需要单个缓冲区一定连续。所以通过片段来面熟缓冲区即使缓冲区分散在内存的多个位置上，内核还是可以进行块IO；这样的Vector IO被称为scatter-gatter IO；

