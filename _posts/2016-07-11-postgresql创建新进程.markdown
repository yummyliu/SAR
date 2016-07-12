---
layout: post
title: postgresql创建新进程
date: 2016-07-11 21:07
header-img: "img/head.jpg"
header-img: "img/head.jpg"
tags:
    - PG
---

> 现在需要在现有postgresql进程的基础上，另加上一个服务进程，来做元数据信息同步，首先来整理一下 pg的现有的进程结构，以及相互之间的关系

### 进程简述

##### 进程

```
/home/liuyangming/postgresql-9.5.3/build/bin/postgres -D data
postgres: checkpointer process
postgres: writer process
postgres: wal writer process
postgres: autovacuum launcher process
postgres: stats collector process
postgres: liuyangming tpcds [local] EXPLAIN // 响应客户端请求fork的进程，其他为常驻的后台辅助服务进程
```

##### 对应文件

```
autovacuum.c      // 系统自动清理进程的源文件
bgworker.c        // 维护一个BackgroundWorkerArray,其中维护着后台工作进程的列表
		  // 各个进程都可以访问，postmaster无锁访问，其他进程要或得排它锁才能写，共享锁才能读
bgwriter.c        // 后台写进程的源文件
checkpointer.c    // 检查点进程，掌控系统的所有的检查点，pg 9.2后新加的
fork_process.c    // 启动进程工具函数
pgarch.c          // 预习式日志归档进程的源文件
pgstat.c          // 统计数据收集进程的源文件
postmaster.c      // Postmaster
startup.c         
syslogger.c       // 系统日志进程的源文件
walwriter.c       // 预写式日志写进程的源文件
```
看了一天进程启动的代码，想看看在哪个地方可以修改  
可以创建一个新的后台进程，  
最终没想到在postgresql 9.5中已经提供了一个[background worker progress][bwp]  
开始没有吧bgworker.c和进程对应上，没注意看源文件里的注释啊！！！  
**// POSTGRES pluggable background workers implementation**

### postgresql 启动流程
> postgresql 9.5 版本不同顺序不同

虽然找到了接口，附带追了一下postgresql启动创建进程的流程，以下是我简单画了一个流程图，供君指点 :)
![pgstartup](/image/postgres启动.jpg)

### 正题 background worker

```
typedef struct BackgroundWorker
{
	char		bgw_name[BGW_MAXLEN];
	int			bgw_flags;
	BgWorkerStartTime bgw_start_time;
	int			bgw_restart_time;		/* in seconds, or BGW_NEVER_RESTART */
	bgworker_main_type bgw_main;
	char		bgw_library_name[BGW_MAXLEN];	/* only if bgw_main is NULL */
	char		bgw_function_name[BGW_MAXLEN];	/* only if bgw_main is NULL */
	Datum		bgw_main_arg;
	char		bgw_extra[BGW_EXTRALEN];
	pid_t		bgw_notify_pid; /* SIGUSR1 this backend on start/stop */
} BackgroundWorker;

/*
	 * If bgw_main is set, we use that value as the initial entrypoint.
	 * However, if the library containing the entrypoint wasn't loaded at
	 * postmaster startup time, passing it as a direct function pointer is not
	 * possible.  To work around that, we allow callers for whom a function
	 * pointer is not available to pass a library name (which will be loaded,
	 * if necessary) and a function name (which will be looked up in the named
	 * library).
	 */
	if (worker->bgw_main != NULL)
		entrypt = worker->bgw_main;
	else
		entrypt = (bgworker_main_type)
			load_external_function(worker->bgw_library_name,
								   worker->bgw_function_name,
								   true, NULL);

	/*
	 * Note that in normal processes, we would call InitPostgres here.  For a
	 * worker, however, we don't know what database to connect to, yet; so we
	 * need to wait until the user code does it via
	 * BackgroundWorkerInitializeConnection().
	 */

	/*
	 * Now invoke the user-defined worker code
	 */
	entrypt(worker->bgw_main_arg);



```


[bwp]: https://www.postgresql.org/docs/9.5/static/bgworker.html
