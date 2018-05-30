---
layout: post
title: 
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PG
---

# 基本介绍

标准的CTE可以很简单的建立一个临时表，这对于组织复杂查询很有帮助。

递归CTE在处理层级数据的时候，更加厉害；类似于在sql中的for循环。递归查询有两部分：

1. anchor：选择递归的初始行，可以有多列

2. recursive：基于初始行，产生更多的数据行；两者之间用`union all`连接，如下一个简单的递归cte

   ```sql
   with recursive cruncher(inc, double, square) as (
     select 1, 2.0, 3.0 -- anchor member
     union all
     select -- recursive member
       cruncher.inc + 1,
       cruncher.double * 2,
       cruncher.square ^ 2
     from cruncher
     where inc < 10
   )
   select * from cruncher
   ```

   递归cte总是返回的是没有重复的数据，因为union；

3. termination condition: 递归终止条件；

# 旅行商问题

n个城市，从其中一个城市出发，经过所有城市，最终回到起点的最短距离；

```sql
--城市数据表
create table places as (
  select
    'Seattle' as name, 47.6097 as lat, 122.3331 as lon
    union all select 'San Francisco', 37.7833, 122.4167
    union all select 'Austin', 30.2500, 97.7500
    union all select 'New York', 40.7127, 74.0059
    union all select 'Boston', 42.3601, 71.0589
    union all select 'Chicago', 41.8369, 87.6847
    union all select 'Los Angeles', 34.0500, 118.2500
    union all select 'Denver', 39.7392, 104.9903
)
-- 距离计算
create or replace function lat_lon_distance(
  lat1 float, lon1 float, lat2 float, lon2 float
) returns float as $$
declare
  x float = 69.1 * (lat2 - lat1);
  y float = 69.1 * (lon2 - lon1) * cos(lat1 / 57.3);
begin
  return sqrt(x * x + y * y);
end
$$ language plpgsql
```

假设从 San Francisco出发，递归的遍历各个城市

```sql
with recursive travel (places_chain, last_lat, last_lon,
    total_distance, num_places) as (
  select -- 初始行
    name, lat, lon, 0::float, 1
    from places
    where name = 'San Francisco'
  union all
  select -- 递归
    -- 添加到访问链中
    travel.places_chain || ' -> ' || places.name,
    places.lat,
    places.lon,
    -- 增加当前距离
    travel.total_distance + 
      lat_lon_distance(last_lat, last_lon, places.lat, places.lon),
    travel.num_places + 1
  from
    places, travel
  where
    -- 当前城市没有出现在访问链中的时候，继续旅行，否则不递归旅行
    position(places.name in travel.places_chain) = 0
)
```

从SF出发，每个数据行就是可能的访问序列，维护一个访问顺序字符串，最后一个城市的坐标，当前走过的距离，以及路过的城市数；

最终，当不存在某个城市，某个序列中他没有经过它；那么递归到这一层时，就结束了；

UNION ALL；就得到了长度为1~8的所有序列；如下可以看到所有的序列

```sql
select * from travel where num_places = 8
```

然而我们还需要回到原点；只需要找到num_place=8的序列中，回到原点的最近距离；order by 2 limit 1 即可

```sql
select
  travel.places_chain || ' -> ' || places.name,
  total_distance + lat_lon_distance(
      travel.last_lat, travel.last_lon,
      places.lat, places.lon) as final_dist
from travel, places
where
  travel.num_places = 8
  and places.name = 'San Francisco'
order by 2 -- ascending!
limit 1

                                                   ?column?                                                    |    final_dist
---------------------------------------------------------------------------------------------------------------+------------------
 San Francisco -> Seattle -> Denver -> Chicago -> Boston -> New York -> Austin -> Los Angeles -> San Francisco | 6670.83798218894
```



一开始我们说到递归CTE使用来处理层次数据，可以这么理解，在一个表上的查询需要自己join自己，或许会join多次才能得到一个结果，典型的例子是，部门表，人员表中有上下级层次关系，如果需要找到某一层级下的所有数据，可以通过union将多次join的数据合并，但是递归的union简化了这一写法，如下：

```sql
INSERT INTO employees (
 employee_id,
 full_name,
 manager_id
)
VALUES
 (1, 'Michael North', NULL),
 (2, 'Megan Berry', 1),
 (3, 'Sarah Berry', 1),
 (4, 'Zoe Black', 1),
 (5, 'Tim James', 1),
 (6, 'Bella Tucker', 2),
 (7, 'Ryan Metcalfe', 2),
 (8, 'Max Mills', 2),
 (9, 'Benjamin Glover', 2),
 (10, 'Carolyn Henderson', 3),
 (11, 'Nicola Kelly', 3),
 (12, 'Alexandra Climo', 3),
 (13, 'Dominic King', 3),
 (14, 'Leonard Gray', 4),
 (15, 'Eric Rampling', 4),
 (16, 'Piers Paige', 7),
 (17, 'Ryan Henderson', 7),
 (18, 'Frank Tucker', 8),
 (19, 'Nathan Ferguson', 8),
 (20, 'Kevin Rampling', 8);
 -- 找到managerid为2下的所有员工
 WITH RECURSIVE subordinates AS (
 SELECT
 employee_id,
 manager_id,
 full_name
 FROM
 employees
 WHERE
 employee_id = 2
 UNION
 SELECT
 e.employee_id,
 e.manager_id,
 e.full_name
 FROM
 employees e
 INNER JOIN subordinates s ON s.employee_id = e.manager_id
) SELECT
 *
FROM
 subordinates;
 
--- putong库region信息，展开
 with recursive subregions as(
 select name,id,parent_id from regions where id = 1
 union
 select 
 r.name,
 r.id,
 r.parent_id
 from regions r
 inner join subregions s on s.id = r.parent_id)
 select * from subregions where parent_id = 1 limit 10;
```

当某一层没有任何数据产生，就不会接着递归了，这里就没有明显的终止条件；

## Loose indexscan

在一些数据库中，使用loose indexscan可以利用btree索引，来高效的找到一列中的唯一值，当索引中有很多重复值的时候，这很高效；

PostgreSQL并不直接支持loose indexscan，但是可以使用递归CTE；查询的递归部分每次处理一个行，找到比上次处理的行大的最小值；然后主查询中，将可能的null值加进去；

```sql
WITH RECURSIVE t AS (
   SELECT MIN(col) AS col FROM tbl
   UNION ALL
   SELECT (SELECT MIN(col) FROM tbl WHERE col > t.col)
   FROM t WHERE t.col IS NOT NULL
   )
SELECT col FROM t WHERE col IS NOT NULL
UNION ALL
SELECT NULL WHERE EXISTS(SELECT 1 FROM tbl WHERE col IS NULL);

-- 如果col定义为NOT NULL
WITH RECURSIVE t AS (
   (SELECT col FROM tbl ORDER BY col LIMIT 1)  -- parentheses required
   UNION ALL
   SELECT (SELECT col FROM tbl WHERE col > t.col ORDER BY col LIMIT 1)
   FROM t
   WHERE t.col IS NOT NULL
   )
SELECT col FROM t WHERE col IS NOT NULL;
```

vs

```
SELECT DISTINCT col FROM tbl;
```

###### 求稀疏列的唯一值

```sql
create table sex (sex char(1), otherinfo text);    
create index idx_sex_1 on sex(sex);    
insert into sex select 'm', generate_series(1,50000000)||'this is test';    
insert into sex select 'w', generate_series(1,50000000)||'this is test'; 
insert into sex select 't', generate_series(1,50000000)||'this is test';    
insert into sex select 'f', generate_series(1,50000000)||'this is test';    

with recursive skip as (
  (
    select min(t.sex) as sex from sex t
  )
  union all
  (
    select (select min(t.sex) as sex from sex t where t.sex > s.sex)
      from skip s where s.sex is not null
  )
)
select * from skip ;
```

###### 求差

把稀疏列的distinct值求出来，然后 not in

#### 有效利用联合索引

```sql
CREATE TABLE tbl (col INTEGER NOT NULL, col2 INTEGER NOT NULL);
INSERT INTO tbl
SELECT trunc(random()*5) AS col, (random()*1000000)::INT AS col2 FROM generate_series(1,10000000);
CREATE INDEX ON tbl (col, col2);
--
SELECT COUNT(*) FROM tbl WHERE col2 = 9854;
-- vs
SELECT COUNT(*) FROM tbl WHERE col2 = 9854 AND col IN 
(
  WITH RECURSIVE
     t AS (SELECT MIN(col) AS col FROM tbl
           UNION ALL
           SELECT (SELECT MIN(col) FROM tbl WHERE col > t.col) FROM t WHERE t.col IS NOT NULL)
  SELECT col FROM t
);
```

> PG的多列索引和MySQL的多列索引的区别；只针对B-tree索引来说；
>
> PG中可以联合索引的任何子集都有可能使用索引；只是有可能，基于代价估计；但是只有当第一列有限制，使用索引的效果才好；
>
> MySQL，如果第一列没有出现在where条件中，就不会用多列索引；order by中的顺序必须和定义索引的时候一样；**最左前缀原则**

[ref](https://www.periscopedata.com/blog/postgres-recursive-cte)

[ref2](http://www.postgresqltutorial.com/postgresql-recursive-query/)

[loose indexscan](https://wiki.postgresql.org/wiki/Loose_indexscan)

[dege](https://github.com/digoal/blog/blob/master/201705/20170519_01.md)