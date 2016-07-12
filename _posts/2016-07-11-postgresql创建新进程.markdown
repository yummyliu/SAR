---
layout: post
title: postgresql创建新进程
date: 2016-07-11 21:07
header-img: "img/head.jpg"
header-img: "img/head.jpg"
tags:
    - PG
---

fork_process
```
/*
 * IsPostmasterEnvironment is true in a postmaster process and any postmaster
 * child process; it is false in a standalone process (bootstrap or
 * standalone backend).  IsUnderPostmaster is true in postmaster child
 * processes.  Note that "child process" includes all children, not only
 * regular backends.  These should be set correctly as early as possible
 * in the execution of a process, so that error handling will do the right
 * things if an error should occur during process initialization.
 *
 * These are initialized for the bootstrap/standalone case.
 */
```
