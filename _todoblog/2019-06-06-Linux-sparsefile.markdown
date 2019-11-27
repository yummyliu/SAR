---
layout: post
title: Linux IO杂谈
date: 2019-06-06 15:52
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - Linux
typora-root-url: ../../yummyliu.github.io
---

* TOC
{:toc}
考虑到文件系统如果支持sparsefile，那么文件的物理大小和逻辑大小是不同的。ll 显示的是逻辑大小，du显示的是按block（默认是1024byte）为单位的物理大小。因此有如下情况，du显示是4（就是4*1024，4k），ll显示的是1025（就是逻辑上的大小）

```bash
# dd if=/dev/zero of=test bs=1 count=1025
1025+0 records in
1025+0 records out
1025 bytes (1.0 kB) copied, 0.00209302 s, 490 kB/s
[12:29:04] nestdb-dev /data/mydata/var
# ll
-rw-r--r-- 1 root root       1025 Aug 13 12:29 test
[12:29:16] nestdb-dev /data/mydata/var
# du test
4       test
```

通过fallocate，可以在文件中打洞，如下，打洞后，du还是按block为单位显示实际占用大小，ls还是逻辑大小

```bash
# dd if=/dev/zero of=test bs=1 count=4097
4097+0 records in
4097+0 records out
4097 bytes (4.1 kB) copied, 0.00877725 s, 467 kB/s

# ls -l
-rw-r--r-- 1 root root       4097 Aug 13 12:31 test

# du test
8       test

# fallocate -n -p -o 4095 -l 5 test

# du test
4       test

# ls -l
-rw-r--r-- 1 root root       4097 Aug 13 12:31 test
```

