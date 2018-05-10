```sql
putong-shard=# select * from rel_8192_6383.moments offset 3378 limit 1;
         id          |   value   | location | user_id  | liked_user_ids |        created_time        |        updated_time        | status  | visibility | muted
---------------------+-----------+----------+----------+----------------+----------------------------+----------------------------+---------+------------+-------
 1308744959404523828 | 全是熟人🤗 | null     | 17291503 |                | 2016-08-03 14:29:38.534465 | 2017-01-06 03:12:53.329229 | deleted |            |
(1 row)

putong-shard=# select * from rel_8192_6383.moments offset 3379 limit 1;
ERROR:  timestamp out of range
putong-shard=# select * from rel_8192_6383.moments offset 3380 limit 1;
         id          |           value           | location | user_id  |                         liked_user_ids                          |        created_time        |        updated_time        | status  | visibility | muted
---------------------+---------------------------+----------+----------+-----------------------------------------------------------------+----------------------------+----------------------------+---------+------------+-------
 1313416723283820872 | 宝宝起床啦😉今天目标圆明园 | null     | 32061679 | {31188566,27984538,21680293,10256672,19070815,5658021,11264566} | 2016-08-10 01:11:36.186987 | 2017-02-21 14:35:11.644981 | deleted |            |
(1 row)

putong-shard=# select created_time,updated_time from rel_8192_6383.moments offset 3379 limit 1;
ERROR:  timestamp out of range
putong-shard=# select updated_time from rel_8192_6383.moments offset 3379 limit 1;
        updated_time
----------------------------
 2016-07-27 05:44:55.174453
(1 row)

putong-shard=# select id,updated_time from rel_8192_6383.moments offset 3379 limit 1;
         id          |        updated_time
---------------------+----------------------------
 1303407427720561615 | 2016-07-27 05:44:55.174453
(1 row)

putong-shard=# update rel_8192_6383.moments set created_time = '2016-07-27 05:44:55.174453' where id = 1303407427720561615;
ERROR:  cannot execute UPDATE in a read-only transaction
putong-shard=# select pg_is_in_recovery();
 pg_is_in_recovery
-------------------
 t
```

