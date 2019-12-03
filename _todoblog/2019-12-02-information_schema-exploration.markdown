---
layout: post
title: MySQL5.7的catalog剖析——information_schema
date: 2019-12-02 16:44
categories:
  - MySQL
typora-root-url: ../../yummyliu.github.io
---
* TOC
{:toc}
MySQL很大的一个特点就是底层有多种存储引擎，其中InnoDB是最复杂的。但是了解了InnoDB并不能很好的了解整体的MySQL架构，因此需要对SQL层有所了解。除去SQL解析部分，存储层有很多存储引擎；

那么，对于SQL层来说，为了兼容各种不同的存储引擎，需要在SQL层维护全局的元数据；分别就存放在四个系统database中：

> 注意MySQL中没有schema的概念，不像PostgreSQL中database之下有一个schema逻辑分割；可是这里数据库的名字又叫***_schema 🤷‍♀️。

1. information_schema
2. performance_schema
3. sys
4. mysql

这里分成几次，逐个攻破每个模块的信息内容以及实现逻辑。本文是第一部分，即，information_schema。

# Information_schema

Information_schema可以看做是MySQL的catalog（PostgreSQL的[catalog](https://www.postgresql.org/docs/11/catalogs.html)），其中有数据库、表、列各个维度的信息。其都是只读的表，用户本身无法进行update/delete/insert。因为information_schema都是通过类似存储引擎扩展的方式，添加的一种新扩展接口——`MYSQL_INFORMATION_SCHEMA_PLUGIN`。

那么，information_schema中的表在底层存储中会放在不同的介质中，如下图：

![img](/image/1203-overview_of_IS.png)

> plugin有很多种，还有熟悉的MYSQL_STORAGE_ENGINE_PLUGIN；以及MYSQL_UDF_PLUGIN，MYSQL_FTPARSER_PLUGIN，MYSQL_AUDIT_PLUGIN等。
>
> 只要你想要在MySQL的系统表中添加一种新的元信息，那么就可以实现这个扩展即可。

由于

# TABLE_SHARE