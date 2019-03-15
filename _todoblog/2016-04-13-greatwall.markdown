---
layout: post
title: 翻墙
date: 2016-04-13 16:45
header-img: "img/head.jpg"
tags:
    - Tool
typora-root-url: ../../yummyliu.github.io
---


# 流水账

之前一直用买的shadowsock来翻墙，到了这个公司，翻不出去了。放弃翻墙了，但是找资料效率低下。
发现有人用aws+shadowsock 在公司内部可以翻墙，于是，决定研究研究，看看能不能翻了这个局域网的墙。

+ 按照已经成功的案例，我也申请了aws的云主机，在上面搞了一个shadowsocks代理服务；配置好本地shadowsock客户端的服务器设定，然而并不管用

	> netstat -anp 相应端口正在监听
	iptables -L | grep <port> 端口也是开放的

+ 研究了一下shadowsock这个原理，其是基于sock5协议的，位于传输层之上，封装了一个udp，tcp连接，可以理解其在会话层，总之，其运行的时候需要本地shadowsock代理向服务器发起一个tcp连接。

+ 于是简单搞了一个tcpserver，将其运行aws的某个端口上。本机的client连接不上，但是在aws上的client可以连上tcpserver；然后我让**不在公司局域网的人，用tcpclient连接tcpserver,可以连接上**。这样，可以确定是公司局域网的问题，使向远程服务器发起的tcp连接失败。

# 问题与猜想

但是，想到了这个ssh下面同样是要向远程服务器发起tcp连接的，难道局域网防火墙，能够识别ssh发起的tcp连接，还是其他程序发起的tcp连接，进而只允许ssh建立连接？

基于上面的猜想+有成功使用aws+shadowsock翻墙的案例，猜测难道后来针对sock5协议，局域网里又做了限制？

就在百愁莫展的准备放弃翻墙的时候，就这么研究了一下ssh协议，原来这么强大，可以利用ssh在本地建一个sock代理，**-qTfnN -D 7070**。

##  附加，温习了一下网络相关的知识：

 服务不可用，netstat -anp 看相依端口有没有启动；iptable -L  看相应的端口有没有被防火墙屏蔽；ping 服务器是否ping 通，ping 这个命令只能说明 网络层 ip解析没问题，icmp没问题；

0.0.0.0 整个网络

`127.`：保留内部回送；
 A:1-126     私有 10
B:128-191   私有 172
C:192-223   私有 192    
网络号全0是本网络的意思，主机号全1广播地址