---
layout: post
title: SQL Puzzle in PostgreSQL
date: 2018-07-25 13:23
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - PostgreSQL
typora-root-url: ../../yummyliu.github.io
---

> [这有](https://www.dbrnd.com/sql-interview-the-ultimate-sql-puzzles-and-sql-server-advance-sql-queries/#)一些SQL server中的一些SQL问题，
> 同样的问题，在PostgreSQL中如何解决？

# Get the last three Records of a table

```sql
SELECT *
FROM tbl_testtable
OFFSET
  (SELECT count(1)-3
   FROM tbl_testtable) LIMIT 3;
```

# Find Correlation Coefficients for the Run of Cricket Players

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

# Delete Duplicate Data without Primary key

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

# Generate Calendar Data for 19th Century

```sql
SELECT split_part(generate_series::text,' ',1)
FROM generate_series('1990-01-01'::TIMESTAMP, '2000-01-01', '1 days');
```

# Use Recursive CTE, and list out the Years from Dates

```sql
 WITH RECURSIVE cte AS
  ( SELECT '2001-09-28'::TIMESTAMP AS dt
   UNION ALL SELECT dt + interval '1 years'
   FROM cte
   WHERE dt < '2009-09-28')
SELECT date_part('year',dt)
FROM cte;
```

# Calculate the Power of Three

```sql
WITH RECURSIVE cte AS
  (SELECT 1 AS power_of_3
   UNION ALL SELECT power_of_3 * 3
   FROM cte
   WHERE power_of_3 < 100)
SELECT *
FROM cte;
```

# Find the Median Value from the Given Number

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

# Use Group By, Find MIN MAX unit sold of a Month

```sql
select 
	Month_No
	,min(Total_Unit_Sold) as Min_Sales
	,max(Total_Unit_Sold) as Max_Sales 
from tbl_Products
group by Month_No
```

# Get the list of Monday of a Month

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

