---
layout: post
title: 追postgresql的点滴(持续添加)
date: 2016-07-09 23:25
categories: jekyll update
---


### 关于limit

postgresql 现在有个portal 来运行
关于查询结果的返回：查询的描述结构中有一个 DestReceiver
DestReceiver中的receiveSlot是一个回调函数，
execute plan 函数参数中有一个numberTuple 每次执行返回结果就是最大numberTuple的。
使用limi 3t参数，发现这里还是0，追起来发现，这个numbertuple设置为0就是处理所有的数据，limit设定为3的时候，是和返回值相关，executeplan 中有个死循环，只有当number都处理完，或者TupisNull才会退出，所以limit 设定为3 ，返回的结果就是三个元素，而关于返回结果的输出，
代码中，有两种输出方式：text和binary，使用psql的时候 是走的text方式，另一个binary 猜测是走的libpg odbc？

### netezza 转 pg

由于tpcds没有 pg的 sql查询，直接执行有一些问题
1、日期加减，，14 days -> interval 14 day
2、子查询要有别名
3、grouping
4、ERROR:  failed to find conversion function from unknown to text 
5、![icov](/image/icovn.png)

### over()窗口函数

窗口函数也是计算一些行集合（多个行组成的集合，我们称之为窗口window frame）的数据，有点类似与聚集函数（aggregate function）。但和常规的聚集函数不同的是，窗口函数不会将参与计算的行合并成一行输出，而是保留它们原来的样子

### oom

执行tpcds99个查询的时候，部分查询出现oom，起初希望性能好一点，所以把work_mem和share_buffer设置的比较大，现在将share_buffer调小了，部分oom消失了，可是还是有部分查询存在oom的问题，绕了一圈，发现，work_mem是针对操作符级别的用来作为 sort，hash_table的内存空间，这样我把work_mem调大了，当有操作符需要work_mem的时候就申请不到空间了

