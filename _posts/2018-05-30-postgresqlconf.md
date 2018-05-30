layout: post
title: 
date: 2018-02-05 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:

    - PG

# 配置参数

listen_addresses = '*'
port = 5432
max_connections = 300
superuser_reserved_connections = 10

# - Memory -
+ `shared_buffers` = 128Mb 
  + 数据库系统的缓冲区，默认128Mb，（如果系统资源并不够，取决于initdb的初始化）；一般还要用OS的缓冲区，所以只设置成内存的25%，不超过40%；该参数变大，相应的`max_wal_size`也要变大。Q1
+ `huge_pages`: 
  + 只在linux下有效，默认 try；减小页表大小和CPU时间，提高性能
+ `temp_buffers`: 
  + session localbuffer，访问自己的临时表，
+ `work_mem` = 8MB:
  + 默认4mb, 内部的sort（`ORDER BY`, `DISTINCT`, merge joins）和hashtable（hash joins, hash-based aggregation, hash-based processing of `IN` subqueries）用的，超过了就写到 temporary disk file里
+ `maintenance_work_mem` = 64MB
  + 默认64mb，一般比work_mem大，因为维护的指令少 
+ autovacuum_work_mem = 96MB
  + 默认是-1，那么就取maintennance_work_mem的值
+ max_stack_depth = 2MB；
  + 默认2mb，一般由kernel控制，postgresql检测kernel的限制
+ temp_file_limit = -1 (nolimit);
  + work_mem 不够了，就交换到这里
+ `dynamic_shared_memory_type` = 'mmap'
  + mmap不是默认值，因为OS会反复修改磁盘文件增加磁盘负载。当 `pg_dynshmem`目录被存储在一个 RAM 盘时或者没有其他共享内存功能可用时， 它还是有用的。Q2
+ `replacement_sort_tuples`:  [replacement_sort][http://www.cs.bilkent.edu.tr/~canf/CS351Fall2010/cs351lecturenotes/week4/index.html ], 小于这些行，就不用快排
+ `max_prepared_transactions`: 分布式事务的两阶段提交，设置事务状态 prepared

# - Cost-Based Vacuum Delay -

在VACUUM和ANALYZE命令执行的时候，Postgresql 系统内部维护一个对各种IO操作的预估代价的统计值，当这个值达到vacuum_cost_limit时，系统会让这些进程sleep一段时间（vacuum_cost_delay），然后重置，避免这些操作影响整体系统性能

+ vacuum_cost_delay = 10ms
+ vacuum_cost_limit = 10000
+ vacuum_cost_page_hit = 1 ： 清理共享缓存中的page的代价
+ vacuum_cost_page_miss = 10：清理磁盘中page的代价
+ vacuum_cost_page_dirty = 20：vacuum修改了一个之前clean的page，需要再次flush到disk中 ，这一操作的代价 Q3

# - Background Writer -
为了提高insert，update，delete的性能，pg并不是当时写入磁盘，而是通过bgwriter进程周期性地将sharebuffer中的脏页写到disk中。写的太快会导致多次写同一个页，写的太慢会影响后来的查询，通过下面的参数来控制。

+ `bgwriter_delay` = 10ms
  + 间隔时间
+ bgwriter_lru_maxpages = 200
  + 每次最多写出的page
+ bgwriter_lru_multiplier = 2.0
  + 默认值2.0；用来估计下一轮次需要的写出的page数，来保证有足够的干净缓冲区

# - Asynchronous Behavior -
+ `effective_io_concurrency` = 1
  + 执行的并发磁盘IO数量，对于特定的表空间的表，可以设定表空间上的同名参数
+ `max_worker_processes` = 3  
  + PG可以编写扩展插件，加载动态链接库，运行用户提供的代码。相应代码放在shared_preload_libraries中，可以在Postgresql启动的时期初始化为bgworker。

# - WRITE AHEAD LOG -
+ wal_level = 'replica'
+ fsync = on
  + 直接写磁盘
+ synchronous_commit = off
  + commit的时候是否等待备库完成
+ wal_sync_method = 'fdatasync'
  + 同步写磁盘的系统调用方法
+ full_page_writes = on
  + 全页写，避免部分写导致的恢复问题
+ wal_buffers = 16MB
  + 共享内存中，用来存储还没写入磁盘的wal日志；当同时有很多commit的时候，这个值太大影响性能
+ wal_writer_delay = 20ms
  + wal flush的时间间隔。
+ commit_delay = 20
  + fsync如果是off，no delay；否则，每次commit会delay一段时间，这是为了提高group commit的吞吐量，使得一个wal flush可以commit多个Transaction。但是如果需要提交的事务小于commit_siblings，那么就不会delay
+ commit_siblings = 9
  + commit会delay的最小同时提交事务数

# - Archiving -
archive_mode = on

archive_command = ''

+ archive_timeout = 0
  + 强制系统定期切换新的wal段文件，以便archive去归档，因为只归档已经完成的wal段文件，但是强制切换之后，wal段文件和主动切换的大小一样，所以这个值如果设，不要设的太小，浪费disk空间。而0是禁止。

# - Sending Server(s) -
max_wal_senders = 20

+ wal_keep_segments = 6000 
  + master 最少保留的wal段文件，以便standby来进行流复制
+ wal_sender_timeout = 30s
  + 超时的话，master终止repication连接。

# - Master Server -
synchronous_standby_names = ''

+ vacuum_defer_cleanup_age = 50000
  + 当数据行对所有事务都不可见时，应该被清理了，但是如果有热备，由于主从之间存在延迟，这些清理的的行在备机可能是可见的。这就产生冲突了。这个参数可以用来配置standby节点上面的发生这种冲突时recover的apply延迟。意思就是主机那边不着急清理，等待vacuum_defer_cleanup_age个事务之后再清理，

# - Standby Servers -
hot_standby = on
max_standby_archive_delay = 10min
max_standby_streaming_delay = 3min
wal_receiver_status_interval = 1s

+ hot_standby_feedback = on
  + 解决与vacuum_defer_cleanup_age类似的问题，向主机反馈查询取消了
+ wal_receiver_timeout = 30s
  + 超时的话，master终止repication连接

# - Planner Method Configuration -
enable_bitmapscan = on
enable_hashagg = on
enable_hashjoin = on
enable_indexscan = on
enable_indexonlyscan = on
enable_material = on
enable_mergejoin = on
enable_nestloop = on
enable_seqscan = on
enable_sort = on
enable_tidscan = on

# - Planner Cost Constants -
seq_page_cost = 1
random_page_cost = 1.1
cpu_tuple_cost = 0.01
cpu_index_tuple_cost = 0.005
cpu_operator_cost = 0.0025
effective_cache_size = 10GB

# - Where to Log -
http://zhangwensheng.cn/blog/post/vincent/about_postgresql_conf-log

log_destination = 'csvlog'
logging_collector = on
log_directory = 'pg_log'
log_filename = 'postgresql-%a.log'

log_file_mode = 0600
log_truncate_on_rotation = on
log_rotation_age = 1d

+ 日志超过一天则切换，

log_rotation_size = 128MB

+ 日志大于128m 则切换

# - When to Log -
client_min_messages = notice

+ 客户端返回信息的级别

log_min_messages = warning

+ 日志记录信息的最小级别

log_min_error_statement = debug1

+ 所有导致一个debug1级别的语句被记录

log_min_duration_statement = 100

# - What to Log -
debug_print_parse = off
debug_print_rewritten = off
debug_print_plan = off
debug_pretty_print = off
log_checkpoints = on
log_connections = off
log_disconnections = off
log_duration = off
log_error_verbosity = 'default'
log_hostname = off
log_line_prefix = '%t [%p:%l] user=%u,db=%d,app=%a,client=%h '
log_lock_waits = on
log_statement = 'ddl'
log_temp_files = -1
log_timezone = 'PRC'
constraint_exclusion='partition'

# - Query/Index Statistics Collector -
track_activities = on
track_counts = on
track_io_timing = on
track_functions = all

+ track_activity_query_size = 4096
  + pg_stat_activity.current_query 保留的大小
+ update_process_title = off
  + 根据新的查询更新进程名称

stats_temp_directory = 'pg_stat_tmp'

+ default_statistics_target = 1000
  + analyze的时候的取样颗粒度，越大采样效果越好，也越耗时间

# - Statistics Monitoring -
log_parser_stats = off
log_planner_stats = off
log_executor_stats = off
log_statement_stats = off

# - AUTOVACUUM PARAMETERS -
autovacuum = on

+ log_autovacuum_min_duration = '1min'
  + 记录超过1min的autovacuum动作

autovacuum_max_workers = 3
autovacuum_naptime = 10min

+ autovacuum_vacuum_threshold = 200
  + autovacuum_vacuum_scale_factor*table_size+autovacuum_vacuum_threshold时，进行vacuum。
+ autovacuum_analyze_threshold = 100
  + autovacuum_analyze_scale_factor*table_size+autovacuum_analyze_threshold时，进行analyze。

autovacuum_vacuum_scale_factor = 0.2
autovacuum_analyze_scale_factor = 0.05
autovacuum_freeze_max_age = 600000000
autovacuum_multixact_freeze_max_age = 400000000
autovacuum_vacuum_cost_delay = -1
autovacuum_vacuum_cost_limit = -1

# - Checkpoints -
+ max_wal_size = 10GB


+ min_wal_size = 10GB

checkpoint_timeout = 50min
checkpoint_completion_target = 0.9
checkpoint_warning = 60s

# - Statement Behavior -
search_path = '"$user",public'
default_tablespace = ''
temp_tablespaces = ''
check_function_bodies = on
default_transaction_isolation = 'read committed'
default_transaction_read_only = off
default_transaction_deferrable = off
session_replication_role = 'origin'
statement_timeout = 0
lock_timeout = 0
vacuum_freeze_min_age = 50000000
vacuum_freeze_table_age = 150000000
vacuum_multixact_freeze_min_age = 5000000
vacuum_multixact_freeze_table_age = 150000000
bytea_output = 'hex'
xmlbinary = 'base64'
xmloption = 'content'

# - Locale and Formatting -
datestyle = 'iso, mdy'
intervalstyle = 'postgres'
timezone = 'PRC'
timezone_abbreviations = 'Default'
extra_float_digits = 0
client_encoding = sql_ascii
lc_messages = 'C'
lc_monetary = 'C'
lc_numeric = 'C'
lc_time = 'C'
default_text_search_config = 'pg_catalog.english'

# - Other Defaults -
dynamic_library_path = '$libdir'
local_preload_libraries = ''
session_preload_libraries = ''
shared_preload_libraries = 'pg_stat_statements, auto_explain'

auto_explain.log_min_duration = '10s'
auto_explain.log_analyze = true
auto_explain.log_verbose = true
auto_explain.log_timing = true
auto_explain.log_nested_statements = true

pg_stat_statements.max = 10000
pg_stat_statements.track = all

# - LOCK MANAGEMENT -
deadlock_timeout = 100ms
max_locks_per_transaction = 256
max_pred_locks_per_transaction = 64



### 生产环境重启

1. 备库执行checkpoint
2. 重启备库
3. 主库看流复制是够正常，不正常说明备库挂了
4. 重启主库



## Question

Q1:

A: 发生 1.用户主动 2.系统定时(5min)的checkpoint 3.wal日志超过`max_wal_size`(1G)，checkpoint进程会将shard_buffers中的脏数据（包括表和索引）刷到磁盘上；max_wal_size控制着wal日志的大小，如果share_buffers变大，如果`full_page_write`打开，意味着每次checkpoint需要写更多的数据，这时，如果`max_wal_size`过小，就有可能很快又超过`max_wal_size`，意味着又要checkpoint，而频繁的checkpoint影响系统性能。

如果没有设置`archive_mode=on`，当超过max_wal_size，会根据min_wal_size重用wal日志

Q2: 我们系统出什么原因使用mmap共享内存？

Q3: 不太明白这一操作是如何发生的？

