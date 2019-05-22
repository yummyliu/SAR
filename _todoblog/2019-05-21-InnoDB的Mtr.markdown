---
layout: post
title: 
date: 2019-05-21 16:35
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---

* TOC
{:toc}
# 数据一致性保证

FIX

WAL

Force-log-at-commit

# 代码

include文件中有一些`.ic`结尾的文件，其中放的是一些inline的函数[bug](https://bugs.mysql.com/bug.php?id=91885)。

mtr_t的结构在include/mtr0mtr.h中，这是控制mtr的关键结构，下面了解几个主要成员的含义：

+ `m_made_dirty`：mtr has made at least one buffer pool page dirty

+ `m_modifications`：the mini-transaction modified buffer pool pages；**乍一看，修改了buffer pool pages 难道不是和变脏是同一个概念么？**

























### 参考

[innodb-mtr](https://www.kancloud.cn/digest/innodb-zerok/195089)