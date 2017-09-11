---
layout: post
title: undefine-reference错误
date: 2016-03-20 22:37
categories: jekyll update
tags:
    - Linux
---

> 写makefile编译一个工程，需要添加 glog 和 gtest的库 
> 首先按照 常规的 ./configure make make install 就行了,
> 这里这两个包需要 使用cmake 由 CMakeLists.txt 来生成makefile,
> 后面make之后 gtest 不支持make install，glog make install 也是有问题，既然安装有问题，那就就自己来连接吧


按照 链接库的顺序，
在 ~/ 目录下创建了 mylib的文件夹，将 libgtest.a libgtestmain.a libglog.a 拷贝过来
在 ~/ 目录下创建了 myhead的文件夹，将相关头文件拷贝过来

配置环境变量 CPLUS_INCLUDE_PATH , LIBRARY_PATH(静态) , LD_LIBRARY_PATH(动态)
makefile中生成 .o文件的时候，加上头文件，使其能找到 符号声明
连接的时候，加上 $(LDFLAGS) (定义的连接哪些库，以及动态还是静态连接的指令)

按理说现在这样已经可以连接成功编译了
可是总是出现提示glog库中的undefine-reference的问题，怀疑是连接方式不对，库编译不对等等....

最后居然是因为，我把 $(LDFLAGS) 放在了连接对象的前面

**在UNIX类型的系统中，编译器和连接器，当命令行指定了多个目标文件，连接时按照自左向右的顺序来搜索外部函数的定义。也就是说，当所有调用这个函数的目标文件名列出后，再出现包含这个函数定义的目标文件或库文件。否则，就会出现找不到函数的错误，连接是必须将库文件放在引用它的所有木匾文件之后**
