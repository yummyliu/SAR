Index可以加速查询，但是也有一些缺点，需要避免；

1. **use up space**：提高Physical Backup的空间和时间
2. **slow down data modification**：该表连带该index
3. **prevent HOT updates**：写放大

Index收益：

1. 加速where查询
2. b-tree加速max() 和 min()
3. 加速order by
4. 加速join
5. 有外键的时候，delete的时候避免seq scan
6. unique用来当constrain
7. 编译优化器更好的统计 数值分布

##### 找到unusedindex

```sql
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



SELECT 
       pg_relation_size(s.indexrelid) AS index_size
FROM pg_catalog.pg_stat_user_indexes s
   JOIN pg_catalog.pg_index i ON s.indexrelid = i.indexrelid
WHERE s.idx_scan = 0      -- has never been scanned
  AND 0 <>ALL (i.indkey)  -- no index column is an expression
  AND NOT EXISTS          -- does not enforce a constraint
         (SELECT 1 FROM pg_catalog.pg_constraint c
          WHERE c.conindid = s.indexrelid)
ORDER BY pg_relation_size(s.indexrelid) DESC;



```



测试：

```bash
psql -Atq - -Udba -dputong-shard -h $(tdb s1.s) -f unusedindex.sql |  awk '{sum += $1};END {print sum}'
```

