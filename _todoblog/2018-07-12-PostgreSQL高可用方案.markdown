---
layout: post
title: 
date: 2018-07-12 17:16
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
    - K8S
typora-root-url: ../../yummyliu.github.io
---
http://www.databasesoup.com/2018/07/should-i-run-postgres-on-kubernetes.html
```
a "source of truth" to prevent split-brain;
something to ensure that the minimum number of replicas were running;
routing to the current master that can be quickly changed;
a way to enforce shutdown of rogue nodes;
a UI to view all of the members of my cluster.
```

max_wal_size可能在一些情况下超出限制；

关于checkpoint_segment的限制并没有提，测试一下会不会超出这个值;

- 测试：

  ![](/image/PastedGraphic-1.png)

  ![](/image/PastedGraphic-2.png)checkpoint_segment设置为3； 1. checkpoint_segment的意思是: 当wal日志达到这个的时候会开始执行checkpoint；但是执行checkpoint的时候还是会产生新的日志；如上图xlog并不是3而是5个；2. 手动执行一次checkpoint，观察xlog的变化，可见xlog数量没有变化，但是旧的xlog给重命名，用来循环利用(从文件时间，即可看出wal日志的先后)；虽然重命名了，但是日志的数量并没有缩减到3（checkpoint_segment配置的），所以可以看出xlog没有达到数量的上限；只是循环利用，但是没有缩减；
  经过推断，wal日志的上限是< checkpoint_segment *2 ，达到checkpoint_segment的时候执行一次checkpoint，由于checkpoint不是立马结束的，所以会保留两个checkpoint相关的xlog日志；3. 继续测试，随着我不断的写，xlog不断循环利用，xlog的数量稳定在5，5<3*2; 符合推断；4. 并且从PostgreSQL 11 的Release Note中提到，在新版本中，对wal有两个新的改进：    不再保留持续两个checkpoint的wal记录，只保留一个checkpoint的相关wal记录    为了提高压缩率，将强制切换的旧版本wal，都填充为05. 针对线上checkpoint_segment的设置：( (60000*16MB / 1024 ) *2 - 739)=1135GB ，如果还剩1135G，并且base文件不涨，可以稳定，但是但是现在只剩了650G
  可能的解决方案：1. 删除profile，释放空间2. 适当减小 checkpoint_segment ，降低一下上限