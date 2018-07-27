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
