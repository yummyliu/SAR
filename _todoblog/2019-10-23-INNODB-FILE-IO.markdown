---
layout: post
title: 
date: 2019-10-23 17:30
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---
> * TOC
{:toc}

问题？磁盘上存在一个文件，但是通过如下方式打开，偶发性地报文件不存在的错误

```cpp
os_file_create(
  ..._file_key,
  filename,
  OS_FILE_OPEN,
  OS_FILE_AIO,
  OS_LOG_FILE,
  srv_read_only_mode,
  &success
)
```

文件名最后的\0 没有成功复制过来。







原来以为O_DIRECT就直接落盘了，现在发现自己理解错了，这个参数只是绕过了page cache而已；想要写的时候直接落盘，是和参数 O_SYNC 和 O_DSYNC有关（这两个参数的write，可以看做write+fsync），我又看了些文档里对这两个参数的区分解释；分别对应了 file integrity和data integrity；

有个解释：To understand the difference between the two types of completion, consider two pieces of file metadata: the file last modification timestamp (st_mtime) and the file length. All write operations will update the last file modification timestamp, but only writes that add data to the end of the file will change the file length. The last modification timestamp is not needed to ensure that a read completes successfully, but the file length is. Thus, O_DSYNC would only guarantee to flush updates to the file length metadata (whereas O_SYNC would also always flush the last modification timestamp metadata).





# 参考

[zhenghe](https://zhenghe.gitbook.io/open-courses/ucb-cs162/topic-ensuring-data-reaches-disk)

[linux open](https://linux.die.net/man/2/open)