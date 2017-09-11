---
layout: post
title: EffectiveGo阅读笔记--
date: 2017-09-11 09:52
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - Go
---

# Pointers & Values

> The rule about pointers vs. values for receivers is that value methods can be invoked on pointers and values,
> but pointer methods can only be invoked on pointers.

因为指针可以修改对象（方法的调用者，在文档中称作receiver）的数据，值类型能够调用指针类型的方法，会产生错误。故从语言层面，
规避了这个问题。
