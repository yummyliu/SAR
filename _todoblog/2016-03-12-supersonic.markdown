---
layout: post
title: supersonic简述
date: 2016-03-12 15:46
categories: jekyll update
tags:
  - Database
---

supersonic 是google 的开源项目，目前版本号只是0.9 基于列存储的SQL查询引擎库，使用c++编写，并大量使用缓存感知，SIMD，超流水线的优化，基于列存储，其面向的是数据仓库中的OLAP应用，测试的时候 使用 TPC-H这个针对决策支持系统的数据集
关于SUpersonic的使用，其提供数据库下的典型的操作符，sort agg join 等，采用pull from top 的方式，每个operator 提供next接口，供上层操作符抽取数据

#### supersonic简介

{% highlight c++ %}
    scoped_ptr<Operation> computer2(Compute(compute_expression2, aggregator.release()));
    SortOrder *sort_order = new SortOrder();
    sort_order->add(ProjectNamedAttribute("l_returnflag"), ASCENDING);
    sort_order->add(ProjectNamedAttribute("l_linestatus"), ASCENDING);
    scoped_ptr<Operation> sorter(Sort(sort_order,
            ProjectAllAttributes(),
            128,
            computer2.release()));
    scoped_ptr<Cursor> result_cursor(SucceedOrDie(sorter->CreateCursor()));
    rowcount_t result_count = 0;
    scoped_ptr<ResultView> result_view(new ResultView(result_cursor->Next(-1)));
    while (!result_view->is_done())
    {
        const View &view = result_view->view();
        result_count += view.row_count();

        // do something。。
        result_view.reset(new ResultView(result_cursor->Next(-1)));
    }

{% endhighlight %}

这是从代码中找的一个典型的使用，其中操作符都继承自Operation类，Operation中定义了一个 纯虚函数 createcursor
类似的游标类系中，Cursor作为基类中其中定义了一个Next纯虚函数

#### supersonic 内存分配

{%highlight c++ %}
    class Buffer {
      public:
       void* data() const { return data_; }
       size_t size() const { return size_; }
      private:
       friend class BufferAllocator;
       Buffer(void* data, size_t size, BufferAllocator* allocator)
             : data_(CHECK_NOTNULL(data)), size_(size), allocator_(allocator) {}
       void Update(void* new_data, size_t new_size) {
           data_ = new_data;
           size_ = new_size;
       }
       void* data_;
       size_t size_;
       BufferAllocator* const allocator_;
    };

{% endhighlight %}

Buffer将构造函数私有，并设置一个BufferAllocator的友元类，由且只由该类分配buffer，并以该类为基类拥有面向不同场景的内存分配类
