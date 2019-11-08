---
layout: post
title: 
date: 2019-11-02 21:44
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---
> * TOC
{:toc}

首先是最近在想一个问题PostgreSQL为什么 25%，然后MySQL的是80%？

# Double buffer

data_sync_retry

sync_file_range

Reducing spinlock acquisition within clock sweep loop

# fsync

![image-20191105095057874](/image/1103-sharedbuffer.png)



![image-20191105095408352](/image/1103-sb01.png)



![image-20191105095920881](/image/1103s-sb3.png)



![image-20191105100307725](/image/1103-sb4.png)

MySQLdon not fsync twice

![image-20191105100429669](/image/1103-sb5.png)

[Improving Postgres' Buffer Manager]



![image-20191105100912869](/image/1103-sb6.png)

![image-20191105101129665](/image/1103-sb7.png)



![image-20191105104252743](/image/1103-sb8.png)

![image-20191105104559076](/image/1103-sb9.png)



![image-20191105104638463](/image/1103-sb10.png)



![image-20191105105211335](/image/1103-失败1.png)

Oracle使用Direct IO；早期开发人员很少，尽量利用Kernel提供的特性，减少开发量；从而能专注在数据库研究中。

怎么做？



![image-20191105105931315](/image/1103-sb12.png)



![image-20191105110053966](/image/1103-sb13.png)

之后可能也用direct io。

Http://lwn.net/Articles/752063



![image-20191105110415363](/image/1103-sb14.png)

https://billtian.github.io/digoal.blog/2016/09/28/01.html



[PostgreSQL used fsync incorrectly for 20 years](https://fosdem.org/2019/schedule/event/postgresql_fsync/)



[data_sync_retry](https://www.postgresql.org/docs/10/runtime-config-error-handling.html#GUC-DATA-SYNC-RETRY)

https://news.ycombinator.com/item?id=11512653

https://wiki.postgresql.org/wiki/Fsync_Errors

https://www.percona.com/blog/2019/02/22/postgresql-fsync-failure-fixed-minor-versions-released-feb-14-2019/

By default, panic instead of retrying after `fsync()` failure, to avoid possible data corruption (Craig Ringer, Thomas Munro)

Some popular operating systems discard kernel data buffers when unable to write them out, reporting this as `fsync()` failure. If we reissue the `fsync()` request it will succeed, but in fact the data has been lost, so continuing risks database corruption. By raising a panic condition instead, we can replay from WAL, which may contain the only remaining copy of the data in such a situation. While this is surely ugly and inefficient, there are few alternatives, and fortunately the case happens very rarely.

A new server parameter [data_sync_retry](https://www.postgresql.org/docs/11/runtime-config-error-handling.html#GUC-DATA-SYNC-RETRY) has been added to control this; if you are certain that your kernel does not discard dirty data buffers in such scenarios, you can set `data_sync_retry` to `on` to restore the old behavior.





https://git.postgresql.org/gitweb/?p=postgresql.git;a=commitdiff;h=5d7962c6797c0baae9ffb3b5b9ac0aec7b598bc3



Increase the number of buffer mapping partitions to 128



Change locking regimen around buffer replacement



读

Write Scalability – Concurrency Bottlenecks





https://www.thomas-krenn.com/en/wiki/Linux_Storage_Stack_Diagram



[https://www.redhat.com/en/topics/data-storage/file-block-object-storage#:~:text=File%20storage%20organizes%20and%20represents,links%20it%20to%20associated%20metadata.](https://www.redhat.com/en/topics/data-storage/file-block-object-storage#:~:text=File storage organizes and represents,links it to associated metadata.)





 Currently PostgreSQL supports DirectIO only for WAL, but it is unusable on practice 

• Requires a lots of development 

• Very OS specific 

• Allows to use specific things, like O_ATOMIC 

• PostgreSQL is the only database, which is not using Direct IO





```
Sync_file_rage system call is interesting. But it was supported only by Linux 
kernel 2.6.22 or later. In postgresql, it will suits Robert's idea which does not 
depend on kind of OS.
```

https://billtian.github.io/digoal.blog/2016/09/28/01.html



http://yoshinorimatsunobu.blogspot.com/2014/03/how-syncfilerange-really-works.html



https://git.postgresql.org/gitweb/?p=postgresql.git;a=commit;h=428b1d6b29ca599c5700d4bc4f4ce4c5880369bf



On linux it is possible to control this by reducing the global dirty
limits significantly, reducing the above problem. But global
configuration is rather problematic because it'll affect other
applications; also PostgreSQL itself doesn't always generally want this
behavior, e.g. for temporary files it's undesirable.





[checkpoint sort](https://git.postgresql.org/gitweb/?p=postgresql.git;a=commitdiff;h=9cd00c457e6a1ebb984167ac556a9961812a683c)  sort by tablespace, relfilenode, fork and block number.  often result in a lot of writes that can be
coalesced into one flush.





all writes since the
last fsync have hit disk" but we assume it means "all writes since the last
SUCCESSFUL fsync have hit disk".





```c

    int ret = fsync(file);

    if (ret == 0) {
      return (ret);
    }

    switch (errno) {
      case ENOLCK:

        ++failures;
        ut_a(failures < 1000);

        if (!(failures % 100)) {
          ib::warn(ER_IB_MSG_773) << "fsync(): "
                                  << "No locks available; retrying";
        }

        /* 0.2 sec */
        os_thread_sleep(200000);
        break;

      case EIO:

        ib::fatal() << "fsync() returned EIO, aborting.";
        break;


```

clear-error-and-continue