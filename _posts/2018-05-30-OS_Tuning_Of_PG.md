---
layout: post
title: 在Linux系统中，PostgreSQL调优详解
date: 2018-12-24 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
    - Linux
---

#  在Linux系统中，PostgreSQL调优详解

> 基于PostgreSQL10

## 存储

###内存申请

#### NUMA

NUMA是一种CPU的硬件架构，和NUMA相对的有SMP，SMP中所有CPU争用一个总线来访问内存；而NUMA称作**非对称性内存访问**；每个CPU有自己的Memory Zone，访问自己的很快，访问别人的慢。

那么每个CPU使用访问那个Memory Zone很明显会影响到性能；事实上NUMA的内存分配策略默认是localalloc（还有preferred、membind、interleave），即从本地node分配，如果本地node分配不了，根据系统参数：**vm.zone_reclaim_mode**，决定是否从其他node分配内存：

+ 取0，系统倾向于从其他node分配内存
+ 取1，系统倾向于从本地节点回收Cache

当系统中有多个Node，且zone_reclaim_mode=1时；可能内存不能完全利用起来，如果你对内存的使用，更偏向于cache的应用而不是数据局部性的应用，那么建议取0；

在PostgreSQL中，我们通常取0，并且会在bios中间numa关闭：

```shell
grubby --update-kernel=/boot/vmlinuz-$(uname -r) --args="numa=off"
```

#### Shared Memory

PostgreSQL有自己的`shared_buffer`，在<=9.2时，PostgreSQL基于的是SYSTEM V的api，SYSTEM V的接口要求设置SHMMAX大小。而>=9.3之后，PostgreSQL基于POSIX的api实现。

> SHMMAX：单个进程可以分配的最大共享内存段的大小；
>
> SHMALL：系统范围的总共享内存大小。
>
> | System V                                                     | Posix                                                   |
> | ------------------------------------------------------------ | ------------------------------------------------------- |
> | Shared Memory 接口调用：shmget(), shmat(), shmdt(), shmctl() | Shared Memory接口调用：shm_open(), mmap(), shm_unlink() |

PostgreSQL的shared_buffer推荐配置是内存的1/4大小，有个别情况配置为大于3/4；但是不要配置为1/4~3/4之间的值。

> why?

#### Over Commit

Linux 是允许memory overcommit；因为Linux的内存申请和分配是两码事，内存分配发生在内存使用的瞬间。因为应用往往会申请比需要的多；

所以在不允许overcommit的情况下，如果申请了2MB内存，但是实际使用了1MB，那么存在内存浪费。而当允许overcommit时，如果没有swap，可能会存在OOM；这是系统的OOM Killer会选择若干个进程杀掉来释放内存（或者配置vm.panic_on_oom，使系统重启）。

Linux的overcommit根据vm.overcommit_memory的取值不同，有三种策略：

- 0 – Heuristic overcommit handling。这是缺省值，它根据某种算法决定是否可以overcommit。

- 1 – Always overcommit。总是允许overcommit，
- 2 – Don’t overcommit。禁止超过阈值的overcommit。

在PostgreSQL中，往往采用的是第三种，而第三种的阈值是通过vm.overcommit_ratio或vm.overcommit_kbytes来设置的，在`/proc/meminfo`中，可以看到阈值CommitLimit的大小：

```bash
$ grep -i commit /proc/meminfo
CommitLimit:    396446012 kB
Committed_AS:   83477808 kB
```

Committed_AS是已经申请（不是分配）的内存的大小，通过`sar -r`可以查看内存的使用情况：

```bash
$ sar -r
Linux 2.6.32-696.el6.x86_64 (ymtest)    12/24/2018      _x86_64_        (56 CPU)

12:00:01 AM kbmemfree kbmemused  %memused kbbuffers  kbcached  kbcommit   %commit
12:10:01 AM   7650220 389141532     98.07    335416 373474200  83456440     17.99
```

其中，*kbcommit*对应的就是Committed_AS，而`%commit`为`Committed_AS/(MemTotal+SwapTotal`。

> CommitLimit计算
>
> `CommitLimit = (total RAM * vm.overcommit_ratio / 100) + Swap`
>
> 如果使用了hugepage，
>
> `CommitLimit = ([total RAM] – [total huge TLB RAM]) * vm.overcommit_ratio / 100 + swap`

### 内存访问

#### HugePages

在Linux中，内存页默认是4k。当内存很大时，如果页很小，整体性能就会变差。PostgreSQL中只支持Linux中的HugePage（在BSD中有Super Page，在Window中有Large Page），启用了`huge_pages`后，相应的内存页表就会变小，进而会提高内存管理的性能。

Linux的HugePage大小从2MB到1Gb不等；默认是2MB，大小在系统启动的时候设置。

```bash
>> cat /proc/meminfo | grep -i huge
AnonHugePages:         0 kB
HugePages_Total:       0
HugePages_Free:        0
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
```

在如上的配置中，尽管huge page设置成2MB，但是HugePages_Total为0，意思是还未启动HugePage。

基于如下脚本的运行结果，我们可以设置配置相应的HugePage的大小。

```bash
#!/bin/bash
PGDATA="???"
pid=`head -1 $PGDATA/postmaster.pid`
echo "Pid:            $pid"
peak=`grep ^VmPeak /proc/$pid/status | awk '{ print $2 }'`
echo "VmPeak:            $peak kB"
hps=`grep ^Hugepagesize /proc/meminfo | awk '{ print $2 }'`
echo "Hugepagesize:   $hps kB"
hp=$((peak/hps))
echo Set Huge Pages:     $hp
```

```
sysctl -w vm.nr_hugepages= 88
```

[这里](https://www.percona.com/blog/2018/12/20/benchmark-postgresql-with-linux-hugepages/)有关于HugePage配置的BenchMark，但是具体情况具体分析，在我们的线上并没有开启；

> Transparent Huge Pages (THP)
>
> 该选项一般也是强制关闭的，因为这个选项系统会在你不执行的情况下，将4kb的page换成大页；可能会导致数据库崩溃等不可控的错误，在bios中关闭；
>
> grubby --update-kernel=/boot/vmlinuz-$(uname -r) --args="transparent_hugepage=never"

#### TLB

> TLB，Translation Lookaside Buffer
>
> CPU用来管理虚拟存储和物理存储的控制线路叫**MMU（内存管理单元**）。(一般是在bootloader中的eboot阶段的进入main()函数的时候启用MMU模块，如果没有启动MMU模块，CPU内核发出的地址将直接送到内存芯片上；有了MMU模块，会截获CPU内核发出的（虚拟）地址，让将虚拟地址转换成物理地址发送到CPU芯片上。
>
> 但是引入MMU后，读取指令和数据的时候，都需要读取两次内存（1. 查询页表得到物理地址；2. 然后访问具体物理地址）。TLB就是页表的Cache，每行保存由单个PTE（Page Table Entry）组成的块，提高虚拟地址到物理地址的转换速度，通常称为快表。
>
> TLB和CPU Cache都是微处理器的硬件结构。CPU cache是基于cpu和memory之前的缓存器，而TLB只是当CPU使用虚拟地址的时候用来加速转换速度的。

**TLB exhaustion**

>  "hidden" memory usage

每个进程都需要小部分内存缓存其使用所有的PTE，可以查询`/proc/$PID/status`中的VmPTE，得知PTE的大小。PTE的大小取决于进程访问的数据范围，如果PostgreSQL的进程访问了shared_buffer的所有页，那么VmPTE比较高。这时，如果链接数很多，那么可能会占用很多内存；

```shell
for p in $(pgrep postgres); 
do 
	grep "VmPTE:" /proc/$p/status; 
done | awk '{pte += $2} END {print pte / 1024 / 1024}'
```

当PTE的size比较大的时候，显然TLB的空间会不足，那么就会TLB miss，进而影响性能。

所以，PostgreSQL尽量和pooler配合使用，减少链接数；或者考虑使用hugepage，减少pte数量；或者减少PostgreSQL的shared_buffer大小，减少每个进程的pte数据量；

#### SwapSpace

```bash
>> free
             total       used       free     shared    buffers     cached
Mem:     396791752  389366352    7425400   20016756     330424  373701172
-/+ buffers/cache:   15334756  381456996
Swap:     67108860          0   67108860
>> swapon -s
Filename                                Type            Size    Used    Priority
/dev/sda2                               partition       67108860        0       -1
```

​	交换空间是安装系统的时候配置的一个磁盘分区或者一个文件，用于提高系统的虚拟内存大小，并且如果系统内存不够可以暂时使用交换空间，避免OOM。

​	但是在PostgreSQL系统中，希望能有较高的响应速度，希望能尽量使用内存，而不是换出去，因此会将系统参数`vm.swappiness`设为较小的值。

​	`vm.swappiness`代表系统对交换空间的喜欢程度，越高越喜欢交换出去。如果设置成0，系统的OOM killer可能会杀死自己的进程，比较安全的是设置为1（默认是60）；

### 内存回写

#### DirtyPage Flush

当内存变脏了，需要将其刷出内存：

+ vm.dirty_background_ratio/vm.dirty_background_bytes

  内存中允许存在的脏页比率或者具体值，达到改值，后台刷盘。取决于外存读写速度的不同，通常将vm.dirty_background_ratio设置为5，而vm.dirty_background_bytes设置为读写速度的25%。

+ vm.dirty_ratio/vm.dirty_bytes

  前台刷盘会阻塞读写，一般vm.dirty_ratio设置的比vm.dirty_background_ratio大，设置该值确保系统不会再内存中保留过多数据，避免丢失。

+ **vm.dirty_expire_centisecs**

  脏页在内存中保留的最大时间。

+ **vm.dirty_writeback_centisecs**

  刷盘进程（pdflush/flush/kdmflush）周期性启动的时间

在PostgreSQL中，自己管理自己的shared Momery。但是并不是所有的读写都是走shared memory；类似地，也有自己的checkpointer和bgwriter进程，负责刷写shared memory。

### 外存

#### 临时文件大小

> temp_file_limit

#### 进程最多打开文件数

> max_files_per_process

## 计算

### 查询解析

### 并行查询

shared-memory-error
http://postgresql.freeideas.cz/shared-memory-error/



overcommit_memory

http://engineering.pivotal.io/post/virtual_memory_settings_in_linux_-_the_problem_with_overcommit/

## PostgreSQL中与系统相关的参数

shared_buffers

huge_pages

temp_buffers

max_prepared_transactions

work_mem

maintenance_work_mem

replacement_sort_tuples

autovacuum_work_mem

max_stack_depth

dynamic_shared_memory_type

temp_file_limit

max_files_per_process

## Linux的内核关键参数





http://linuxperf.com/?p=102

https://rjuju.github.io/postgresql/2018/07/03/diagnostic-of-unexpected-slowdown.html

https://www.geeksforgeeks.org/whats-difference-between-cpu-cache-and-tlb/

http://linuxperf.com

http://www.cnblogs.com/wjoyxt/p/4804081.html