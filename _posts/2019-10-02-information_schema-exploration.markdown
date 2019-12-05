---
layout: post
title: MySQL的TABLE_SHARE简析（5.7）
date: 2019-12-02 16:44
categories:
  - MySQL
typora-root-url: ../../yummyliu.github.io
---
* TOC
{:toc}
MySQL很大的一个特点就是底层有多种存储引擎，其中InnoDB是最复杂的。但是了解了InnoDB并不能很好的了解整体的MySQL架构，因此需要对SQL层有所了解。SQL层就是处理SQL请求，包含connection pool、parser、optimizor、access control以及table define cache等结构，本文主要讨论上层的table define cache，简称tdc，包括以下内容：

+ tdc在内存中的结构
+ tdc在外存中的结构

MySQL的C/S protocol中，定义了多种不同的[命令类型](https://dev.mysql.com/doc/internals/en/text-protocol.html)；根据不同的类型在处理的时候进行分发处理。本文中，我们只讨论COM_QUERY类型。

# TDC

在SQL层中，我们从开始解析SQL的时候会生成一个TABLE_LIST。然后与TDC中比较；最终打开表的时候，会从TABLE_SHARE中取出TABLE，更新TABLE_LIST的版本`set_table_ref_id`。

![image-20191205113513580](/image/1203-sql.png)

tdc主要是一个TABLE_SHARE的结构，通过hash表维护了信息。

在每个查询线程中有自己的TABLE_LIST`*thd->lex->select_lex->get_table_list()->table->s`，这是线程本地Table_cache，其还是从TABLE_SHARE中取出的信息。查找的时候首先从本地的table_cache取出信息，没找到从TABLE_SHARE中取，如果TABLE_SHARE还是没有就从frm中取。

![image-20191205122624326](/image/1203-TDC.png)

当用户进行DDL时，表定义变更会重新创建一个新的frm文件。在替换的时候会调用`close_frm`，`closefrm`中会调用`release_table_share`将过期的TABLE_SHARE条目删除。

> 调用release_table_share，将表的TABLE_SHARE从**table_def_cache**和**oldest_unused_share**中删除。当然，有时候一些操作如close table会调用release_table_share，来减少表对应的TABLE_SHARE的引用计数，如果引用计数为0了，但是版本没发生变化，则将其放入到TABLE_SHARE尾部，并且不会将其从**table_def_cache**中删除。下次，如果在使用该表，则无需磁盘上读取。

TABLE_SHARE中维护了几个链表，其中放了一些Table_cache，可以在线程之前复用，这就是为啥叫table definition cache。tdc就是frm文件的缓存，这是用户不可见的，但是Information_schema是用户可见的。

# information_schema

在系统初始化后，`show databases`命令可以看到4个数据库，但是数据目录中只有三个performance_schema/sys/mysql，却没有information_schema；因为，informationa_schema是MySQL的catalog，但是不是专门存储在一个文件夹下。

Information_schema可以看做是MySQL的catalog（PostgreSQL的[catalog](https://www.postgresql.org/docs/11/catalogs.html)），其中有数据库、表、列各个维度的信息。其都是只读的表，用户本身无法进行update/delete/insert。因为information_schema都是通过类似存储引擎扩展的方式，添加的一种新扩展接口——`MYSQL_INFORMATION_SCHEMA_PLUGIN`。

> plugin有很多种，还有熟悉的MYSQL_STORAGE_ENGINE_PLUGIN；以及MYSQL_UDF_PLUGIN，MYSQL_FTPARSER_PLUGIN，MYSQL_AUDIT_PLUGIN等。
>
> 只要你想要在MySQL的系统表中添加一种新的元信息，那么就可以实现这个扩展即可。

那么，information_schema中的表在底层存储中会放在不同的介质中，如下图，其在8.0之后，为了要准备支持事务性DDL，将元信息都放在InnoDB中：

![img](/image/1203-overview_of_IS.png)

上面说了，TDC是frm的缓存，而information_schema就是TDC对外的呈现形式。我们其实通过通过`reload_acl_and_cache`加`REFRESH_TABLES`参数的方式对TDC进行刷新，因此也就能够更新information_schema中的系统视图。
