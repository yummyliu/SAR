---
layout: post
title: (译)PostgreSQL vs MySQL
subtitle: When considering which database to use for your business, don’t make the mistake of thinking that all open source RDBMS are the same!
date: 2016-08-03 20:54
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - Database
  - PostgreSQL
  - MySQL
---

PostgreSQL和MySQL之间有很大的不同，在评估过两者不同，并做好权衡后决定，才是明智的；这里提供了一个两者之间经常被评估的特性：

+ Open Source
+ ACID合规性
+ SQL合规性
+ Replication
+ Performance
+ Security
+ Cloud Hosting
+ Community Support
+ Concurrency Support
+ NoSQL/JSON Suport
+ Materialized Views & Temporary Tables
+ GeoSpatial Data Support
+ Programming Languages Support
+ Extensible Type System

尽管两者有很多相同的地方，也有很多相当不同的地方。我们尽力提供一个公平并且准确的两者之间的比较，但是最后，需要你基于你的应用场景来评估，决定哪个更加适合你的情况；

明显我们支持PostgreSQL，但是也有有一些情况下，MySQL更加适合。

##### Open Source

开源软件有自己独有的优势——低成本，灵活性，自由，安全并且负责任的，这在一些软件解决方案中有卓越的优势。开源软件是免费获得的，并且可以修改并重新分发；开源软件有很长的生命周期并且永远在技术的前沿。被全世界范围内的组织和个人支持者创建和支持，很多人以和开源合作和志愿为生。

| PostgreSQL                                                   | MySQL                                                        |
| ------------------------------------------------------------ | ------------------------------------------------------------ |
| PostgreSQL被PostgreSQL全球开发组织开发，其中包括多个公司和个人。它是免费并且开源的软件。PostgreSQL基于PostgreSQL协议发布，和BSD和MIT协议很像。 | MySQL基于GNU发布了源代码，以及各种专有协议。现在被Oracle拥有，并提供了多个付费的版本。 |

##### ACID Compliance

ACID是数据库事务的基本特性。ACID合规保证了，在系统失败甚至单个事务中执行多次更改时，没有数据损失和误操作；

| PostgreSQL                                                 | MySQL                                                |
| ---------------------------------------------------------- | ---------------------------------------------------- |
| PostgreSQL是完全支持ACID特性，并且确保所有的要求是符合的； | MySQL只有使用InnoDB和NDB集群存储引擎的时候才是支持的 |

##### SQL Compliance

SQL Compliance是数据库必须遵守和实现的标准，来执行结构化查询。当公司想在多个数据库系统上集成工作，这是很重要的；遵循SQL标准，使得数据库迁移变得容易的多；

| PostgreSQL                                                   | MySQL                                                        |
| ------------------------------------------------------------ | ------------------------------------------------------------ |
| PostgreSQL是一个符合更多的SQL规范。与SQL规范的一致性在PostgreSQL文档的Appendix D中，如果有偏差在Reference中找到；*PostgreSQL支持SQL：2011大部分的的特性，核心部分的179个强制要求，支持160个。并且当前任何dbms没有完全支持sql：2011* | MySQL的某些版本是部分支持（比如不支持CHECK约束）*该产品的一个主要目标是不断完善来符合sql规范，但是没有满意的性能和可靠性。我们不介意在SQL中添加额外的扩展汇或者支持非SQL的特性来提升用户可用性* |

##### Replication

数据库复制是从一个节点往另一个节点上频繁的数据拷贝，这样所有的用户可以共享相同的信息。结果就是一个分布的数据库，这样用户可以访问和他相关的数据而不影响其他人；

| PostgreSQL                                                   | MySQL                                                        |
| ------------------------------------------------------------ | ------------------------------------------------------------ |
| PostgreSQL支持Master-Standby复制，并且引入了强力的优化，使得更快的wal传输处理，这样PostgreSQL中有个几乎实时的热备；<br /><br />PostgreSQL支持的复制：一对一；一对多；热备/流复制；双向复制；逻辑流复制；级联复制。 | MySQL支持Master-Standby复制<br /><br />MySQL中支持的复制类型：一对一；一对多；一个master对应一个standby，这个standby对应一个或者多个；回环复制（ A to B to C and back to A）;Master to Master； |

##### Performance

性能能够基于一些潜在的场景测试出来，因为这个特定的用户需求和应用环境相关；

| PostgreSQL                                                   | MySQL                                                        |
| ------------------------------------------------------------ | ------------------------------------------------------------ |
| PostgreSQL广泛用在一些大型系统中，读写速度很关键并且数据需要保证有效的。另外，PostgreSQL支持一些只在商业系统（比如Oracle，SQL server）中存在性能优化，比如地理信息查询和没有读锁的并发，等等；<br /><br />整体来说，PostgreSQL性能在需要复杂查询时表现好；<br /><br />PostgreSQL在OLTP/OLAP系统中表现不错，这些系统要求读写速度和一定的数据分析需求<br /><br />PostgreSQL在BI应用中表现良好，但是更加要求较大读写速度的数仓和数据分析应用； | MySQL在基于web的应用中应用广泛，这些应用中只是需要一个数据库来处理数据存取的事务。在高负载和复杂查询中性能欠佳；<br /><br />在只读的OLTP/OLAP应用中表现良好<br />MySQL+InnoDB在OLTP中有较高的读写速度。整理来说，MySQL适合高并发的场景；<br /><br />MySQL在BI应用中是可用的并且表现良好，因为BI应用是只读的。 |

##### Security



















##### Comparison Summary
