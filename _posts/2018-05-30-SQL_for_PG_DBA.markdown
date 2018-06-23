---
layout: post
title:
date: 2018-05-30 11:45
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
---



# 常用命令

+  查看表的大小

   ```sql
   SELECT
      relname,
      pg_size_pretty(pg_total_relation_size(relid)) as total,
      pg_size_pretty(pg_total_relation_size(relid) - pg_relation_size(relid)) as table
      FROM pg_catalog.pg_statio_user_tables
      where relname='relanmedddd' and schemaname='snameddd'
      ORDER BY pg_total_relation_size(relid) DESC;
   ```

+ 统计表具体count值

   ```sql
   SELECT
      relname AS objectname,
      relkind AS objecttype,
      reltuples AS "#entries", pg_size_pretty(relpages::bigint*8*1024) AS size
      FROM pg_class
      WHERE relpages >= 8 and relname~'relnameddd'
      ORDER BY relpages DESC;
   ```


+ find unused index

   ```sql
   -- Completely unused indexes:
   SELECT relid::regclass as table, indexrelid::regclass as index
        , pg_size_pretty(pg_relation_size(indexrelid))
     FROM pg_stat_user_indexes
     JOIN pg_index
    USING (indexrelid)
    WHERE idx_scan = 0
      AND indisunique IS FALSE order by pg_relation_size(indexrelid);
   ```

+ find multiple index

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

+ useless index

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

+ 杀查询

   ```sql
   select pg_cancel_backend(pid) from pg_stat_activity where pid <> pg_backend_pid() and application_name !~ 'psql'; \watch 0.5
   ```


+ rebuild index

   ```sql
   CREATE UNIQUE INDEX CONCURRENTLY user_pictures_new_pkey_new ON yay.user_pictures USING btree (id)

   BEGIN;
   DROP INDEX CONCURRENTLY user_pictures_new_pkey;
   ALTER INDEX user_pictures_new_pkey_new RENAME TO user_pictures_new_pkey;
   COMMIT;
   ```


+ Rebuild pk

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


+ Lock check

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

   ```sql
   SELECT relation::regclass, * FROM pg_locks WHERE NOT GRANTED;

   ```

+ 检查线上查询

    ```sql
    select split_part(split_part(application_name,':',1),'-',2) as appname ,left(split_part(query,'FROM',2),100) from pg_stat_activity where usename !='dba' and client_addr = '127.0.0.1' and application_name !='';
    ```
