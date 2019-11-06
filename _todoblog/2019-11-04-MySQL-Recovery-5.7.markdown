---
layout: post
title: 
date: 2019-11-04 12:18
header-img: "img/head.jpg"
categories: jekyll update
tags:
typora-root-url: ../../yummyliu.github.io
---
> * TOC
{:toc}



1. set recv_recovery_on
2. flushList的红黑树
3. 找到可用的checkpoint_lsn（写的时候是两个地方轮换着写， 避免当前写失败，那么还有一个备的）
4. 