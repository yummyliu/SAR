---
layout: post
title: 
date: 2019-06-11 10:51
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---

* TOC
{:toc}
# Philosophy redo 

1. 数据页的变更必须通过mtr，在`mtr_commit()`中将redo记录吸入redo日志。
2. 一般来说，变更之前会调用`mlog_write_ulint()`函数(或其他类似函数)。
3. 对于一些页面级别的操作，只在记录中记录C函数的编码和参数，解决空间。
   1. 不需要在`trx_undo_header_create() ,trx_undo_insert_header_reuse()`的记录中加参数；
   2. 不能添加不做任何改变的函数，或者需要依赖页外部数据的函数；当前log模块的函数有完备的页面转换，没有足够的理由不要擅自改动。

# insert

理想情况下的插入，不考虑页面分裂，分为4mtr：

1. 分配undo段空间，写undo记录
2. 写undo记录
3. 写数据行
4. 2pc commit

如下图：



![image-20190611161857692](/image/mysql-insert.png)