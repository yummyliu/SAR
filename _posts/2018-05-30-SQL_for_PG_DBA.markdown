---
layout: post
title: PostgreSQL DBA常用的SQL(RQ)
subtitlle: 频繁重用的代码片段整理，有需要可以看看
date: 2018-05-30 11:45
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
---

* TOC
{:toc}
# 表的情况

## 查看表的大小

```sql
SELECT
   relname,
   pg_size_pretty(pg_total_relation_size(relid)) as total,
   pg_size_pretty(pg_total_relation_size(relid) - pg_relation_size(relid)) as table
   FROM pg_catalog.pg_statio_user_tables
   where relname='relanmedddd' and schemaname='snameddd'
   ORDER BY pg_total_relation_size(relid) DESC;
```

## 年龄最大的表

```sql
SELECT b.nspname,
       a.relname ,
       age(a.relfrozenxid) AS maxtableage,
		pg_size_pretty(pg_total_relation_size(a.oid)) as tablesize,
  (SELECT age(pg_database.datfrozenxid)
   FROM pg_database
   WHERE datname !~ 'postgres|temp') AS age
FROM pg_class a
JOIN pg_namespace b ON b.oid = a.relnamespace
WHERE a.relfrozenxid != 0
ORDER BY maxtableage DESC ;

SELECT 'vacuum freeze verbose ' || b.nspname||'.'||a.relname||';' 
FROM pg_class a
JOIN pg_namespace b ON b.oid = a.relnamespace
WHERE a.relfrozenxid != 0
ORDER BY age(a.relfrozenxid) DESC ;
```

## 统计表具体count值

```sql
SELECT
   relname AS objectname,
   relkind AS objecttype,
   reltuples AS "#entries", pg_size_pretty(relpages::bigint*8*1024) AS size
   FROM pg_class
   WHERE relpages >= 8 and relname~'relnameddd'
   ORDER BY relpages DESC;
```

# 函数情况

## 找到函数返回值为某个表的函数

```sql
SELECT *
FROM pg_proc
WHERE prorettype IN
    (SELECT oid
     FROM pg_type
     WHERE typname = 'table_name');
```



# 索引情况

## 无用索引

```sql
-- Completely unused indexes:
SELECT relid::regclass as table, indexrelid::regclass as index
     , pg_size_pretty(pg_relation_size(indexrelid))
  FROM pg_stat_user_indexes
  JOIN pg_index
 USING (indexrelid)
 WHERE idx_scan = 0
   AND indisunique IS FALSE order by pg_relation_size(indexrelid);
   
 SELECT s.schemaname,
       s.relname AS tablename,
       s.indexrelname AS indexname,
       pg_size_pretty(pg_relation_size(s.indexrelid)) AS index_size
FROM pg_catalog.pg_stat_user_indexes s
   JOIN pg_catalog.pg_index i ON s.indexrelid = i.indexrelid
WHERE s.idx_scan = 0      -- has never been scanned
  AND 0 <>ALL (i.indkey)  -- no index column is an expression
  AND NOT EXISTS          -- does not enforce a constraint
         (SELECT 1 FROM pg_catalog.pg_constraint c
          WHERE c.conindid = s.indexrelid)
ORDER BY pg_relation_size(s.indexrelid) DESC;
```

## 重复索引

```sql
--- Finds multiple indexes that have the same set of columns, same opclass, expression and predicate -- which make them equivalent. Usually it's safe to drop one of them, but I give no guarantees. :)

SELECT pg_size_pretty(SUM(pg_relation_size(idx))::BIGINT) AS SIZE,
       (array_agg(idx))[1] AS idx1, (array_agg(idx))[2] AS idx2,
       (array_agg(idx))[3] AS idx3, (array_agg(idx))[4] AS idx4
FROM (
    SELECT indexrelid::regclass AS idx, (indrelid::text ||E'\n'|| indclass::text ||E'\n'|| indkey::text ||E'\n'||
                                         COALESCE(indexprs::text,'')||E'\n' || COALESCE(indpred::text,'')) AS KEY
    FROM pg_index) sub
GROUP BY KEY HAVING COUNT(*)>1
ORDER BY SUM(pg_relation_size(idx)) DESC;
```

## 用处不大的索引

```sql
CREATE AGGREGATE array_accum (anyelement)
(
    sfunc = array_append,
    stype = anyarray,
    initcond = '{}'
);

select
    starelid::regclass, indexrelid::regclass, array_accum(staattnum), relpages, reltuples, array_accum(stadistinct)
from
    pg_index
    join  on (starelid=indrelid and staattnum = ANY(indkey))
    join pg_class on (indexrelid=oid)
where
    case when stadistinct < 0 then stadistinct > -.8 else reltuples/stadistinct > .2 end
    and
    not (indisunique or indisprimary)
    and
    (relpages > 100 or reltuples > 1000)
group by
    starelid, indexrelid, relpages, reltuples
order by
    starelid ;
```

## 重建索引

```sql
CREATE UNIQUE INDEX CONCURRENTLY user_pictures_new_pkey_new ON yay.user_pictures USING btree (id)

BEGIN;
DROP INDEX CONCURRENTLY user_pictures_new_pkey;
ALTER INDEX user_pictures_new_pkey_new RENAME TO user_pictures_new_pkey;
COMMIT;
```

## 重建主键

```sql
CREATE UNIQUE INDEX CONCURRENTLY user_pictures_new_pkey_new ON yay.user_pictures USING btree (id)

--- make index disvalid
update pg_index set indisvalid = false where indexrelid = 'user_pictures_new_pkey'::regclass;

BEGIN;
SET lock_timeout TO '2s'
alter table user_pictures drop CONSTRAINT user_pictures_new_pkey ;

alter table user_pictures add CONSTRAINT user_pictures_pkey PRIMARY KEY USING INDEX user_pictures_pkey_new
COMMIT;
```

# 锁情况

## 锁依赖

```sql
SET application_name='%your_logical_name%';
SELECT blocked_locks.pid     AS blocked_pid,
         blocked_activity.usename  AS blocked_user,
         blocking_locks.pid     AS blocking_pid,
         blocking_activity.usename AS blocking_user,
         blocked_activity.query    AS blocked_statement,
         blocking_activity.query   AS current_statement_in_blocking_process,
         blocked_activity.application_name AS blocked_application,
         blocking_activity.application_name AS blocking_application
   FROM  pg_catalog.pg_locks         blocked_locks
    JOIN pg_catalog.pg_stat_activity blocked_activity  ON blocked_activity.pid = blocked_locks.pid
    JOIN pg_catalog.pg_locks         blocking_locks
        ON blocking_locks.locktype = blocked_locks.locktype
        AND blocking_locks.DATABASE IS NOT DISTINCT FROM blocked_locks.DATABASE
        AND blocking_locks.relation IS NOT DISTINCT FROM blocked_locks.relation
        AND blocking_locks.page IS NOT DISTINCT FROM blocked_locks.page
        AND blocking_locks.tuple IS NOT DISTINCT FROM blocked_locks.tuple
        AND blocking_locks.virtualxid IS NOT DISTINCT FROM blocked_locks.virtualxid
        AND blocking_locks.transactionid IS NOT DISTINCT FROM blocked_locks.transactionid
        AND blocking_locks.classid IS NOT DISTINCT FROM blocked_locks.classid
        AND blocking_locks.objid IS NOT DISTINCT FROM blocked_locks.objid
        AND blocking_locks.objsubid IS NOT DISTINCT FROM blocked_locks.objsubid
        AND blocking_locks.pid != blocked_locks.pid

    JOIN pg_catalog.pg_stat_activity blocking_activity ON blocking_activity.pid = blocking_locks.pid
   WHERE NOT blocked_locks.GRANTED;
```

## 某个表上的锁情况

```sql
select a.locktype,a.database,a.pid,a.mode,a.granted,a.relation,b.relname,left(c.query, 100)
from pg_locks a
join pg_class b on a.relation = b.oid
join pg_stat_activity c on a.pid = c.pid
where lower(b.relname) = 'users';
```

# 查询

## 慢查询

```sql
SELECT state, backend_xmin,xact_start,left(query,100)
FROM pg_stat_activity
WHERE backend_xmin IS NOT NULL
ORDER BY age(backend_xmin) DESC;
```

## 检查线上查询

```sql
select split_part(split_part(application_name,':',1),'-',2) as appname ,left(split_part(query,'FROM',2),100) from pg_stat_activity where usename !='dba' and client_addr = '127.0.0.1' and application_name !='';
```

## 语句平均执行时间

```sql
CREATE OR REPLACE FUNCTION timeit(insql text)
RETURNS interval
AS $$
DECLARE
    tgtpid bigint;
    startts timestamp;
    sumint interval = '0 seconds';
    rec record;
    i int; numiters int := 1000;
BEGIN
    FOR i IN 1..numiters LOOP
        tgtpid := round(100000 * random());
        startts := clock_timestamp();
        EXECUTE insql INTO rec using tgtpid;
        sumint := sumint + (clock_timestamp() - startts)::interval;
    END LOOP;
    RETURN (sumint / numiters);
END;
$$ LANGUAGE plpgsql;

SELECT timeit(
$$
    SELECT count(1) FROM parent p JOIN detail d ON d.pid = p.id WHERE p.id = $1
$$);
```

# 更新数据

## 删除某几列上重复的记录

只保留ctid或者id或者updatetime等最大的

```sql
with keys as ( select last_value(id) over (partition by user_id, moment_id, moment_user_id order by updated_time), count(*) over (partition by user_id, moment_id, moment_user_id) as c
from tablename)
select * from keys where c > 1;

delete from tablename where id in (select id from keys where c > 1);
```

## 批量间隔更新

```sql
$ cat updateoffset.sql

WITH updateid AS
  (SELECT *
   FROM table_bk
   OFFSET :offset LIMIT 500)
UPDATE table
SET created_time = u.created_time
FROM updateid u
WHERE table.id = u.id;

for (( i = 0; i < 54; i++ )); do
psql -f updateoffset.sql -v offset=$((i*500));
Sleep 60;
done
```

# SHELL

## slave获得masterhost

```bash
MHOST=$(grep primary_conninfo recovery.conf | awk -F 'host=' '{print $2}' | awk '{print $1}')
```

# 复制延迟

```sql
SELECT client_addr,
       pg_wal_lsn_diff(
           pg_current_wal_lsn(),
           sent_lsn
       ) AS sent_lag,
       pg_wal_lsn_diff(
           pg_current_wal_lsn(),
           write_lsn
       ) AS write_lag,
       pg_wal_lsn_diff(
           pg_current_wal_lsn(),
           flush_lsn
       ) AS flush_lag,
       pg_wal_lsn_diff(
           pg_current_wal_lsn(),
           replay_lsn
       ) AS replay_lag
  FROM pg_stat_replication;
```

```sql
SELECT slot_name,
       pg_wal_lsn_diff(
         pg_current_wal_lsn(),
         restart_lsn
       ) as restart_lag,
       pg_wal_lsn_diff(
         pg_current_wal_lsn(),
         confirmed_flush_lsn
       ) as flush_lag
  FROM pg_replication_slots;
```
