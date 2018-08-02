---
layout: post
title: hiveOnspark
subtitle: 介于学校的云平台要升级 原先的虚拟机 用不了了，现在找了个三个闲置的破电脑，搭建了一个小集群试试（2g双核4个物理线程），然而本来我找了这几个电脑是打算搭建一些openstack的，结果发现openstack的搭建环境略高，这几个机器根本满足不了啊！
date: 2016-01-16 08:42
categories: jekyll update
tags:
    - OLAP
---

> 搭建hadoop2.7.1 spark-1.4-without-hive,hive1.2环境

## 首先
    搭建环境 应该了解各个组件之间的工作原理，这样配置的时候出错,可能就知道为什么了，所谓磨刀不误砍柴工
    hive 有三种执行引擎 mr tez spark
    spark 有三种资源调度策略 standalone mesos yarn
    我要搭建的hive on spark，hive 作为hadoop中数据仓库的组件，其完成HQL到查询计划的生成，具体的执行成为一个spark的job spark执行的时候需要向yarn申请所需要的资源
    
    常规的安装步骤 网上有很多，但是还是看官方文档比较好，自己偷懒找了别人得二手知识，有的时候总是会误导你

## 一些值得注意的配置

    要明白spark启动的时候 会需要为那些模块申请空间；
    spark-submit 先不管资源调度方式是什么，后面必定有一个appmaster 和 若干worker，每个worker中有若干executor,但这些executor都是不同的app的,即，通过一个app在同一个worker中只有一个executor，每个executor中会有多个task，这些spark到task这个级别是采用的线程并行的模型，每个sparkapp启动的时候，需要申请excutor的资源给后面的task用

###### yarn的相关资源参数

    **yarn 的资源都是从nodemanager中获得 需要根据各个nodemanager的实际情况配好可用的mem和cpu**
    
    + nodemanager的yarn最大可用内存大小
    
    + nodemanager的虚拟内存/物理内存比率
    
    + nodemanager的cpu核数
    
    + nodemanager的最大可分配cpucore

###### spark 相关资源参数

    **spark在submit的时候 会指定一些参数，这些参数也可以在配置文件中配置**
    
    + driver-memory :
    
    + executor-memory
    
    + executor-cores
    
    + SPARK_WORKER_INSTANCES
    
    + SPARK_WORKER_CORES
    
    + SPARK_WORKER_MEMORY

> First, you should know that 1 Worker (you can say 1 machine or 1 Worker Node) can launch multiple Executors (or multiple Worker Instances - the term they use in the docs).
>
> SPARK_WORKER_MEMORY is only used in standalone deploy mode
> SPARK_EXECUTOR_MEMORY is used in YARN deploy mode
> In Standalone mode, you set SPARK_WORKER_MEMORY to the total amount of memory can be used on one machine (All Executors on this machine) to run your spark applications.
>
> In contrast, In YARN mode, you set SPARK_DRIVER_MEMORY to the memory of one Executor
>
> SPARK_DRIVER_MEMORY is used in YARN deploy mode, specifying the memory for the Driver that runs your application & communicates with Cluster Manager.

