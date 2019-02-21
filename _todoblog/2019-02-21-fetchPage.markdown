---
layout: post
title: 
date: 2019-02-21 11:11
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---
* TOC
{:toc}
# fetch N

```
create table sales (sale_date date, sale_id int4, income int8);
insert into sales values ( generate_series('2019-01-01'::timestamp, '2019-05-01'::timestamp, '1 days'), (random()*100)::int, (random()*1000)::int);
CREATE INDEX sl_dtid ON sales (sale_date, sale_id);
```



Traditional Fetch

```sql
SELECT *
  FROM sales
 ORDER BY sale_date
OFFSET 10
 FETCH NEXT 10 ROWS ONLY;
```

get last position and seek

```sql
SELECT *
  FROM sales
 WHERE sale_date > '2019-01-10'::date
 ORDER BY sale_date
 FETCH FIRST 10 ROWS ONLY;
```

Row Value Compare

```
SELECT *
  FROM sales
 WHERE sale_date > '2019-01-10'::date
 ORDER BY sale_date
 FETCH FIRST 10 ROWS ONLY;
```

