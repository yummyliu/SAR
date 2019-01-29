---
layout: post
title: SQL Puzzle
date: 2018-07-25 13:23
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - PostgreSQL
typora-root-url: ../../yummyliu.github.io
---

> In PostgreSQL

# 表的最近三条记录

```sql
SELECT *
FROM tbl_testtable
OFFSET
  (SELECT count(1)-3
   FROM tbl_testtable) LIMIT 3;
```

# 板球运动员的相关系数查询

```sql
select
  Player1
  ,Player2
  ,(Avg(Runs1 * Runs2) - (Avg(Runs1) * Avg(Runs2))) /
  (stddev_pop(Runs1) * stddev_pop(Runs2)) as PlayersCoefficientRuns
from
(
  select
    a.PlayerName as Player1
    ,a.Runs as Runs1
    ,b.PlayerName as Player2
    ,b.Runs as Runs2
    ,a.MatchYear
  from tbl_Players a
  cross join tbl_Players b
  where b.PlayerName>a.PlayerName and a.MatchYear=b.MatchYear
) as t
group by Player1, Player2;
```

# 没有主键的表，删除重复记录

```sql
WITH cte AS
  (SELECT min(ctid) AS minctid ,
          id
   FROM tbl_testtable
   GROUP BY id)
DELETE
FROM tbl_testtable AS t
WHERE NOT EXISTS
    (SELECT 1
     FROM cte
     WHERE t.ctid = cte.minctid)
  AND t.id =3;
```

# 生成19世纪的日历

```sql
SELECT split_part(generate_series::text,' ',1)
FROM generate_series('1990-01-01'::TIMESTAMP, '2000-01-01', '1 days');
```

# 使用递归SQL，生产某时间后的年份

```sql
 WITH RECURSIVE cte AS
  ( SELECT '2001-09-28'::TIMESTAMP AS dt
   UNION ALL SELECT dt + interval '1 years'
   FROM cte
   WHERE dt < '2009-09-28')
SELECT date_part('year',dt)
FROM cte;
```

# 计算三的幂

```sql
WITH RECURSIVE cte AS
  (SELECT 1 AS power_of_3
   UNION ALL SELECT power_of_3 * 3
   FROM cte
   WHERE power_of_3 < 100)
SELECT *
FROM cte;
```

# 计算中位数

```sql
  CREATE OR REPLACE FUNCTION _final_median(NUMERIC[])
   RETURNS NUMERIC AS
$$
   SELECT AVG(val)
   FROM (
     SELECT val
     FROM unnest($1) val
     ORDER BY 1
     LIMIT  2 - MOD(array_upper($1, 1), 2)
     OFFSET CEIL(array_upper($1, 1) / 2.0) - 1
   ) sub;
$$
LANGUAGE 'sql' IMMUTABLE;
 
CREATE AGGREGATE median(NUMERIC) (
  SFUNC=array_append,
  STYPE=NUMERIC[],
  FINALFUNC=_final_median,
  INITCOND='{}'
);
```

# 计算每个月的最大最小销售记录

```sql
select 
	Month_No
	,min(Total_Unit_Sold) as Min_Sales
	,max(Total_Unit_Sold) as Max_Sales 
from tbl_Products
group by Month_No
```

# 找到每个月的星期一的日期

```sql
SELECT *
FROM
  (SELECT CASE
              WHEN extract(dow
                           FROM t) = 1 THEN t
          END AS dts
   FROM generate_series('2018-01-01'::TIMESTAMP,'2018-02-01','1 days') AS t) as subt
WHERE dts IS NOT NULL;
```

