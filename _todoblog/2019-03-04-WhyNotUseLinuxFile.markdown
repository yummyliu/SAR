---
layout: post
title: 
date: 2019-03-04 16:30
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---
* TOC
{:toc}
# mmap

将外存的文件映射到进程空间的中。linux控制刷盘。

madvise: Tell the OS how you expect to
read certain pages.
mlock: Tell the OS that memory ranges
cannot be paged out.
msync: Tell the OS to flush memory
ranges out to disk.



DBMS (almost) always wants to control things
itself and can do a better job at it.
→ Flushing dirty pages to disk in the correct order.
→ Specialized prefetching.
→ Buffer replacement policy.
→ Thread/process scheduling.
The OS is not your friend



pagesize

![image-20190304164247314](/image/image-20190304164247314.png)

failsafe write



