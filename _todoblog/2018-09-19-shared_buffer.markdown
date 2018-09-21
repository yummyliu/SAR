---
layout: post
title: PostgreSQL的存储引擎概览
date: 2018-09-19 17:13
header-img: "img/head.jpg"
categories: jekyll update
tags: 
  - PostgreSQL
typora-root-url: ../../yummyliu.github.io
---
> * TOC
{:toc}

> - 链接管理与任务调度
> - 关系型查询
> - 事务型存储
>   - Access Methods
>   - Buffer Manager
>   - Lock Manager
>   - Log Manager
> - 准入控制，基于复制冗余的高可用等功能

RDBMS的存储引擎和OS的目标不一样，需要对数据能够完全控制，数据何时应该出现在什么位置？

# PostgreSQL存储引擎

## 访问方法（Access Methods）

> organizing data on disk

```sql
postgres=# SELECT amname, obj_description(oid, 'pg_am') FROM pg_am ORDER BY 1;
 amname |            obj_description
--------+----------------------------------------
 brin   | block range index (BRIN) access method
 btree  | b-tree index access method
 gin    | GIN index access method
 gist   | GiST index access method
 hash   | hash index access method
 spgist | SP-GiST index access method
(6 rows
```

### btree

默认，多路平衡二叉树；大部分的通用数据类型都可以；

### brin

对于超大数据集，如果某一列的值和物理位置有相关性，那么可以使用块范围的索引，索引中存放的是相邻的块的统计信息，比如最大最小值。

### gin

通用倒排索引；一般的索引都是一个key对应一个或多个value；gin是多个key，对于组合类型，即一列中有多个值，的查找采用倒排索引。通常应用gin索引的类型有：

+ hStore：对于一个对象的若干属性，如果某些属性不一定有，可能单独作为一列就很浪费了，这时：

  ```sql
  CREATE TABLE products (
    id serial PRIMARY KEY,
    name varchar,
    attributes hstore
  );
  INSERT INTO products (name, attributes) VALUES (
   'Geek Love: A Novel',
   'author    => "Katherine Dunn",
    pages     => 368,
    category  => fiction'
  );
  
  SELECT name, attributes->'author' as author
  FROM products
  WHERE attributes->'category' = 'fiction'
  ```

+ JSONB (B means Better,binary)

  在JSONB上创建一个GIN索引，就会将这个JSON中所有的key和value都包括进去。

  ```sql
  CREATE TABLE integrations (id UUID, data JSONB);
  INSERT INTO integrations VALUES (
    uuid_generate_v4(),
    '{
       "service": "salesforce",
       "id": "AC347D212341XR",
       "email": "craig@citusdata.com",
       "occurred_at": "8/14/16 11:00:00",
       "added": {
         "lead_score": 50
       },
       "updated": {
         "updated_at": "8/14/16 11:00:00"
       }
     }');
  INSERT INTO integrations (
    uuid_generate_v4(),
    '{
       "service": "zendesk",
       "email": "craig@citusdata.com",
       "occurred_at": "8/14/16 10:50:00",
       "ticket_opened": {
         "ticket_id": 1234,
         "ticket_priority": "high"
       }
     }');
     
  CREATE INDEX idx_integrations_data ON integrations USING gin(data);
  
  ```

  当你的场景中，有很多可选的列，可以考虑使用NoSQL或者schemaless的产品，或者使用PostgreSQL的jsonb；

+ Arrays

+ Range types、

### gist

通用搜索树；

当同一列上的不同值之间是有交集的，建议使用GiST索引：比如使用geometry类型中的多边形（polygon）相交。或者要进行全文检索的text类型。Gist有大小的限制，而GIN可以很大，这就意味着gist索引返回的结果可能是不准确的，但是PostgreSQL会在返回给用户之前进行检查。

### hash

等值查询，目前来说作用有限

### spgist

[SP-GiST: An Extensible Database Index for Supporting Space Partitioning Trees](https://www.cs.purdue.edu/spgist/papers/W87R36P214137510.pdf)

基于上述论文思想的索引，如果数据是自然分区的可以利用这个索引，这不是一个平衡树。常见的例子是用在手机号上，手机号都是固定前缀+数字。但是每个分区可能饱和度不同，那么树就不是平衡的。





## 缓冲区管理（Buffer Manager）

> staging database I/Os

### Why

### What

### How

## 锁管理（Lock Manager）

> concurrency control

## 日志管理（Log Manager）

> recovery 















