---
layout: post
title: 
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PG
---

### 关于sequences

sequences用来产生主键列的数字值；保证产生的New ID是唯一的，即使很多database session同时使用sequence。

PostgreSQL中使用sequences（nextval('my_seq')）的方式和标准SQL不太一样（NEXT VALUE FOR sequence generator name）。

#### sequence不是事务安全的

sequence保证的是唯一性，并不保证连续性。

```SQL
test=# BEGIN;
BEGIN
test=# SELECT nextval('seq_a');
nextval
---------
3
(1 row)
test=# ROLLBACK;
ROLLBACK
test=# SELECT nextval('seq_a');
nextval
---------
4
(1 row)
```

当一个事务运行的时候，db无法预知这个事务是否会commit还是rollback。如果很多事务运行，一部分成功一部分失败，无法追查哪些id成了被释放了。只能记住sequence不能提供连续的id。如果在一些场景中，不允许不连续，那么sequence不适用。

#### New in PG10

##### 列特性

PG10中，引入了标准的SQL方式，定义一个表使用自动生成的唯一值。

```sql
GENERATED { ALWAYS | BY DEFAULT } AS IDENTITY [ ( sequence_options ) ]
```

比如：

```sql
CREATE TABLE my_tab (
   id bigint GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,
   ...
);
--- 等价于如下传统方式
CREATE TABLE my_tab (
   id bigserial PRIMARY KEY,
   ...
);
--- 等价于
CREATE SEQUENCE my_tab_id_seq;
 
CREATE TABLE my_tab (
   id bigint PRIMARY KEY DEFAULT nextval('my_tab_id_seq'::regclass),
   ...
);
 
ALTER SEQUENCE my_tab_id_seq OWNED BY my_tab.id;
```

这里有个问题就是，这样的主键生成的值是默认值，这样如果用户明确的插入一个不同的值，它会覆盖生成的默认值。这通常不是你期望的，因为如果sequence增长到该值，那么会产生约束错误。因此，这样的插入应该是一个错误，利用如下方式避免：

```sql
CREATE TABLE my_tab (
   id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
   ...
);
```

基于这种方式，我们还是可以覆盖生成的值，但是你需要使用``OVERRIDING SYSTEM VALUE` `，这就很难产生错误了。

```sql
INSERT INTO my_tab (id) OVERRIDING SYSTEM VALUE VALUES (42);
```

###### 新的系统表

之前关于sequence的信息，存储在它自己这。现在存储在`pg_sequence`中。

唯一的留在sequence中的数据，就是需要被sequence操作函数(`nextval currval lastval setval`)修改的数据。

#### ALTER SEQUENCE性能回归

在PostgreSQL中，sequence是一个特殊的表，该表只有一行。

在常规表中，修改操作并不修改当前行，而是添加一个新行。之前sequence不是在表中保存，那么`ALTER SEQUENCE`的时候只是修改一行数据，不能rollback。现在在系统表中保存，ALTER SQUENCE就是事务安全的了。

#### one commit

```
commit 3d79013b970d4cc336c06eb77ed526b44308c03e
Author: Andres Freund <andres@anarazel.de>
Date:   Wed May 31 16:39:27 2017 -0700
 
    Make ALTER SEQUENCE, including RESTART, fully transactional.
     
    Previously the changes to the "data" part of the sequence, i.e. the
    one containing the current value, were not transactional, whereas the
    definition, including minimum and maximum value were.  That leads to
    odd behaviour if a schema change is rolled back, with the potential
    that out-of-bound sequence values can be returned.
     
    To avoid the issue create a new relfilenode fork whenever ALTER
    SEQUENCE is executed, similar to how TRUNCATE ... RESTART IDENTITY
    already is already handled.
     
    This commit also makes ALTER SEQUENCE RESTART transactional, as it
    seems to be too confusing to have some forms of ALTER SEQUENCE behave
    transactionally, some forms not.  This way setval() and nextval() are
    not transactional, but DDL is, which seems to make sense.
     
    This commit also rolls back parts of the changes made in 3d092fe540
    and f8dc1985f as they're now not needed anymore.
     
    Author: Andres Freund
    Discussion: https://postgr.es/m/20170522154227.nvafbsm62sjpbxvd@alap3.anarazel.de
    Backpatch: Bug is in master/v10 only
```

这意味着ALTER SEQUENCE会创建一个新的数据文件，当commit的时候，老的文件删掉。这和TRUNCATE CLUSTER VACUUM(FULL)以及部分ALTER TABLE操作类似。必然，这样ALTER TABLE 会比之前更慢，但是这个操作不是那么频繁。

> multi_nextval
>
> ```sql
> --- before PG10
> CREATE OR REPLACE FUNCTION multi_nextval(
>    use_seqname text,
>    use_increment integer
> ) RETURNS bigint AS $$
> DECLARE
>    reply bigint;
> BEGIN
>    PERFORM pg_advisory_lock(123);
>    EXECUTE 'ALTER SEQUENCE ' || quote_ident(use_seqname)
>            || ' INCREMENT BY ' || use_increment::text;
>    reply := nextval(use_seqname);
>    EXECUTE 'ALTER SEQUENCE ' || quote_ident(use_seqname)
>            || ' INCREMENT BY 1';
>    PERFORM pg_advisory_unlock(123);
>    RETURN reply;
> END;
> $$ LANGUAGE 'plpgsql';
>
> --- PG10
> CREATE OR REPLACE FUNCTION multi_nextval(
>    use_seqname regclass,
>    use_increment integer
> ) RETURNS bigint AS $$
> DECLARE
>    reply bigint;
>    lock_id bigint := (use_seqname::bigint - 2147483648)::integer;
> BEGIN
>    PERFORM pg_advisory_lock(lock_id);
>    reply := nextval(use_seqname);
>    PERFORM setval(use_seqname, reply + use_increment - 1, TRUE);
>    PERFORM pg_advisory_unlock(lock_id);
>    RETURN reply + increment - 1;
> END;
> $$ LANGUAGE plpgsql;
> ```
>
> 

#### Fixing Sequence

当COPY或者recreate database的时候，sequence没有及时的更新，会导致数据插不进去。如下SQL来修正Sequence的值，使其从之前的位置开始。

```sql
SELECT 'SELECT SETVAL(' ||
       quote_literal(quote_ident(PGT.schemaname) || '.' || quote_ident(S.relname)) ||
       ', COALESCE(MAX(' ||quote_ident(C.attname)|| '), 1) ) FROM ' ||
       quote_ident(PGT.schemaname)|| '.'||quote_ident(T.relname)|| ';'
FROM pg_class AS S,
     pg_depend AS D,
     pg_class AS T,
     pg_attribute AS C,
     pg_tables AS PGT
WHERE S.relkind = 'S'
    AND S.oid = D.objid
    AND D.refobjid = T.oid
    AND D.refobjid = C.attrelid
    AND D.refobjsubid = C.attnum
    AND T.relname = PGT.tablename
ORDER BY S.relname;
```

使用方式：

1. 将上述文件保存在一个文件中 `reset.sql`

2. 运行该文件，将其输出保存到文件中，然后执行输出结果

   ```bash
   psql -Atq -f reset.sql -o temp
   psql -f temp
   rm temp
   ```

但是，如果table不own这些Sequence，需要运行下面的脚本：

#### Fixing sequence ownership

```sql
SELECT 'ALTER SEQUENCE '|| quote_ident(MIN(schema_name)) ||'.'|| quote_ident(MIN(seq_name))
       ||' OWNED BY '|| quote_ident(MIN(TABLE_NAME)) ||'.'|| quote_ident(MIN(column_name)) ||';'
FROM (
    SELECT 
        n.nspname AS schema_name,
        c.relname AS TABLE_NAME,
        a.attname AS column_name,
        SUBSTRING(d.adsrc FROM E'^nextval\\(''([^'']*)''(?:::text|::regclass)?\\)') AS seq_name 
    FROM pg_class c 
    JOIN pg_attribute a ON (c.oid=a.attrelid) 
    JOIN pg_attrdef d ON (a.attrelid=d.adrelid AND a.attnum=d.adnum) 
    JOIN pg_namespace n ON (c.relnamespace=n.oid)
    WHERE has_schema_privilege(n.oid,'USAGE')
      AND n.nspname NOT LIKE 'pg!_%' escape '!'
      AND has_table_privilege(c.oid,'SELECT')
      AND (NOT a.attisdropped)
      AND d.adsrc ~ '^nextval'
) seq
GROUP BY seq_name HAVING COUNT(*)=1;
```

这个脚本找到孤儿Sequence，运行下面的来check

```sql
SELECT ns.nspname AS schema_name, seq.relname AS seq_name
FROM pg_class AS seq
JOIN pg_namespace ns ON (seq.relnamespace=ns.oid)
WHERE seq.relkind = 'S'
  AND NOT EXISTS (SELECT * FROM pg_depend WHERE objid=seq.oid AND deptype='a')
ORDER BY seq.relname;

```



https://www.cybertec-postgresql.com/en/sequences-gains-and-pitfalls/

https://www.cybertec-postgresql.com/en/sequences-transactional-behavior/

https://wiki.postgresql.org/wiki/Fixing_Sequences