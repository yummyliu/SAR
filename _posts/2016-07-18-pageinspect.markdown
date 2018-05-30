---
layout: post
title: pg的物理存储结构与pageinspect简单介绍
date: 2016-07-18 10:32
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - DB
---

#### PG物理存储

##### 文件布局

`select pg_relation_filepath('relname')`得到文件存储位置，查看/data/base下数据文件，其中对于每个表有如下三个文件

```
-rw------- 1 liuyangming liuyangming   8192 7月   5 12:35 3602
-rw------- 1 liuyangming liuyangming  24576 7月   5 12:35 3602_fsm
-rw------- 1 liuyangming liuyangming   8192 7月   5 12:35 3602_vm
```

+ fsm: free space map of this relation
+ vm:  visibility map ,track which pages are known to have no dead tuples
和mvcc相关
+ 另外 Unlogged tables and indexes 还有一个文件以init为后缀
init: The initialization fork is an empty table or index of the appropriate type. When an unlogged table must be reset to empty due to a crash, the initialization fork is copied over the main fork, and any other forks are erased (they will be recreated automatically as needed).

##### 页布局

> 参考pg9.5文档
> 以下描述中
> byte意味着 8 bits
> 术语item 意思是 存储在page中的数据值.   
> table中,item代表一行row    
> index中,item代表一个索引项.  

表和索引存储在一个固定大小页的数组中，大小一般为8KB（可以在编译期指定不同的page size） 
在表中，所有的page逻辑上都是相同的，所以一个特定的item可以存在任何page中，  
在索引中，第一个page一般被认为是metapage，保存的是控制信息，   
在索引中，可能有不同类型的page，取决于索引访问方法。  

###### Page

![page/image/heap_file_page.png)
Pageheader后面的items是一些(offset,length)对，指向真正的数据位置  
指向一个item的指针叫做 CTID，包含一个页号和item的索引

```
tpcdso100g=# SELECT ctid, * from tmpt;
-[ RECORD 1 ]------
ctid        | (0,1)
sales_price | 0.00
-[ RECORD 2 ]------
ctid        | (0,2)
sales_price | 0.00
```

由page大小的限制，pg的row最大只能有8kb，  
相应最大列数在250到1600之间，取决于列的类型
但是这并不意味着pg的列值限制在8kb内，有一个TOAST机制来处理这一情况

###### Page数据结构
![pgae/image/pagelayout.jpg)
###### PageHeaderData
![header/image/pageheaderdata.jpg)
###### HeapTupleHeaderData 
![pgae/image/heaptupleheader.jpg)
> 得到数据库底层存储页的内容, 只有超级用户有权限使用

##### pageinspect

###### get_raw_page(relname text, fork text, blkno int) returns bytea


fork ：可选main fsm vm init 分别查看指定文件的指定block然后返回bytes的结果

###### page_header(page bytea) returns record

以get_raw_page作为参数，对pg中所有的head页和index页都有效，得到page_header

###### heap_page_items(page bytea) returns setof record

同样是用第一个函数作为参数，得到head页中元素的信息，所有的tuple都被展示出来(不管MVCC是否可见)

等等,了解了物理结构之后，其他调用都是基于get_raw_page的解析,并且可以解析不同索引的结构  
brin gin btree
