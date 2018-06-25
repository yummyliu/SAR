---
layout: post
title: 
date: 2018-05-30 18:06
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - PostgreSQL
---

PG中的字符类型：

| Name                             | Description                |
| -------------------------------- | -------------------------- |
| character varying(n), varchar(n) | variable-length with limit |
| character(n), char(n)            | fixed-length ,blank padded |
| text                             | variable unlimited length  |

这三者在PostgreSQL中性能上没什么差别，除了blanked-padded用到了额外的空间，限制长度会需要一点额外的CPUcycle；在PG中推荐使用text或者character varying；

