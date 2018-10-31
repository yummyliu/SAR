---
layout: post
title: PostgreSQL Query
date: 2018-10-31 17:01
header-img: "img/head.jpg"
categories: jekyll update
tags:
  - PostgreSQL
  - DBMS
typora-root-url: ../../yummyliu.github.io
---
> * TOC
{:toc}

# overview

opsCount * opsCost

## opsCount

> how many rows do we process?

### selectivity

#### ops & selectivity function

```
> histogram_bounds
	A list of values that divide the column's values into groups of approximately equal population. The values in most_common_vals, if present, are omitted from this histogram calculation. (This column is null if the column data type does not have a < operator or if the most_common_vals list accounts for the entire population.)
	
	
= MCVs(most common values && most_common_freqs)

nulls

ndistinct


```





### cardinality





The planner assumes that the two conditions are independent, so that the individual selectivities of the clauses can be multiplied together:

## opsCost