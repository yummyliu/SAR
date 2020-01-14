---
layout: post
title: C++内存管理
date: 2016-08-10 15:58
header-img: "img/head.jpg"
categories: 
    - C++
---

* TOC
{:toc}
理解每一门语言其运行时的状态的关键一步就是了解该语言运行时的内存布局。比如学习Java就要了解JVM运行时结构；这篇文章简单讲述C++运行时的内存布局，便于初学者对C++有一个概况的了解。

# C++内存布局

从如下一个简单的代码开始分析。

```c
#include <stdio.h> 
int main(void) 
{ 
	return 0; 
}
```

通过`size`命令，我们知道文件的布局如下所示，分为三部分：text/data/bss。当代码运行起来，这就是全局区域，另外加上栈和堆就是一个应用进程的整体布局。

```bash
» size a.out
   text    data     bss     dec     hex filename
   1127     540       4    1671     687 a.out
```

## text

text就是代码段。首先我们需要了解代码是如何编译生成的？如[下图](https://www3.ntu.edu.sg/home/ehchua/programming/cpp/gcc_make.html)所示，从源码到机器指令代码中间有4个步骤：

1. 预处理：宏展开，引用头文件
2. 编译：转成汇编代码
3. 汇编：将汇编代码转成机器指令
4. 链接：将多个目标文件和库文件进行链接，生成一整个可执行文件。

![img](../image/GCC_CompilationProcess.png)

其中在第4步中，如果是静态库那么每个可执行文件中都会有一个静态库的代码拷贝。这样最终的可执行文件就会比较臃肿；因此，提出了另一个动态（共享）库的链接方式，基于动态库的链接并不会将库代码整合到可执行文件中，而是在可执行文件执行的时候，如果调用了动态库，那么才进行加载。这样可执行文件比较小，并且可以动态更新模块，更加灵活；但是由于具体调用的时候需要对函数入口进行相对寻址，这样效率上比静态库会慢一些；而且动态库是在系统中单独存放，存在被人误删等异常操作，从而影响可执行文件的稳定。

### 动态连接库的系统共享

比如libc.so就是一个常用的动态库，该动态库被系统大多数进程共享使用。在链接过程中，可执行文件中会创建一个**符号映射表**。在app执行的时候，OS将控制权先给`ld.so`，而不是先给app。`ld.so`会找到并引入的lib；然后将控制权转给app。

> ld搜索lib路径先后顺序:
>
> +  DT_RPATH
> +  LD_LIBRARY_PATH (LIBRARY_PATH是静态库的位置，CPLUS_INCLUDE_PATH是头文件的位置)
> +  /etc/ld.so.cache
> +  DT_RUNPATH
> +  /lib(64) or /usr/lib(64)
>
> **undefine-reference错误**：
>
> 在写MakeFile的时候，常常在文件中定义了`LDFLAGS`变量(描述定义了使用了哪些库，以及相应的参数)。但是最后连接的时候，将`*.o`和`LDFLAGS`进行连接，出现undefine-reference问题，因为库中明明有这些符号，但是却没有找到，奇怪了，原因如下：
>
> **在UNIX类型的系统中，编译连接器，当命令行指定了多个目标文件，连接时按照自左向右的顺序来搜索外部函数的定义。也就是说，当所有调用这个函数的目标文件名列出后，再出现包含这个函数定义的目标文件或库文件。否则，就会出现找不到函数的错误，连接是必须将库文件放在引用它的所有目标文件之后**

对于动态库的全局代码段，每个进程维护通过相对寻址来执行。而对于动态库中的非常量全局变量不是共享的，每个进程一个拷贝。

## data

**已经初始化**的全局变量和静态变量。

## bss

> block started by symbol

**没有初始化**的全局变量和静态变量，一般操作系统对其进行置零初始化。

### 类的成员变量初始化

C++的初始化和赋值是两码事，引用类型以及const类型需要在定义的时候初始化。

C++类中的成员变量的初始化是通过构造函数来进行的。如果希望提高初始化的速度可以采用初始化列表（自定义组合类型初始化，减少了一次默认构造函数的调用），但是初始化的顺序依然还是按照**类中出现的先后顺序**进行初始化。另外，C++类中的static类型可看做是全局静态变量，访问需要加上类域。

构造函数分为三种：

#### 默认构造函数

#### 带参数的构造函数

和普通(成员)函数重载类似，只要参数不同，都可重载，是C++多态特性的一部分。

> 但是普通(成员)函数有返回值，对于**只有返回值不同其他都相同的函数不能重载**，比如如下代码编译有问题
>
> ```c++
> #include<iostream>
> class A
> {
> public:
> 	A (){};
> 	virtual ~A () {};
> 
> 	int foo(int a) {
> 	    return 10;
> 	}
> 
> 	char foo(int a) { // compiler error; new declaration of foo()
> 	    return 'a';
> 	}
> private:
> 	/* data */
> };
> 
> 
> int main()
> {
> 	A a;
>     char x = a.foo();
>     getchar();
>     return 0;
> }
> a.cpp:12:7: error: functions that differ only in their return type cannot be overloaded
>         char foo(int a) { // compiler error; new declaration of foo()
>         ~~~~ ^
> a.cpp:8:6: note: previous definition is here
>         int foo(int a) {
>         ~~~ ^
> a.cpp:23:20: error: too few arguments to function call, single argument 'a' was not specified
>     char x = a.foo();
>              ~~~~~ ^
> a.cpp:8:2: note: 'foo' declared here
>         int foo(int a) {
> ```
>
> 因为在C++最后编译的函数符号有函数名和参数拼接而成，类似这样foo_int_int_；参数不同可以区分，但是返回值不同就区分不了了。

#### 拷贝构造函数

一般情况下系统默认会创建一个默认的拷贝函数，执行的拷贝就是简单将成员变量进行复制。但是如果成员变量有句柄等运行时分配的资源，那么需要定义自己的拷贝构造函数进行深拷贝。

## stack

栈内的变量一般就是在函数内部声明的，或者是函数的形参。其作用域也是在**本次调用**内部可见。但是对于static变量，那么就是多次调用都可见。

## heap

heap一般就是动态申请的空间的位置。一般有两种动态空间申请的方法：new/malloc。

### 内存申请

new vs malloc

| NEW                              | MALLOC                   |
| -------------------------------- | ------------------------ |
| 调用构造函数                     | 不                       |
| 是个操作符                       | 是个函数                 |
| 返回确定的数据类型               | 返回void*                |
| 失败抛出异常，也可`new(nothrow)` | On failure, returns NULL |
| 从free store申请空间             | 从heap申请空间           |
| 编译器计算大小                   | 手动设置大小             |

> Free store和heap的区别就是free store上的空间预审请，迟释放。

### 内存释放

delete和 free()一定要和new/malloc()对应使用。

# 内存泄露处理

C++代码应该最头疼的一个问题，如下一个简单的例子。

``` c
typedef char CStr[100];
...
void foo()
{
  ...
  char* a_string = new CStr;
  ...
  delete a_string;
  return;
}
```

```cpp
void my_func()
{
    int* valuePtr = new int(15);
    int x = 45;
    // ...
    if (x == 45)
        return;   // here we have a memory leak, valuePtr is not deleted
    // ...
    delete valuePtr;
}
 
int main()
{
}
```

当`delete a_string`的时候,就会发生内存泄露，最终内存耗尽（memory exhausted）；

首先我们分配内存后要判断，内存是否分配成功，失败就结束该程序；其次要注意**野指针/悬垂指针问题（Wild pointer/Dangling pointer）**：指针没有初始化，或者指针free后，没有置NULL。

## RAII

**"RAII: Resource Acquisition Is Initialization"**
意思就是任何资源的获取，不管是不是在初始化阶段，都是被一个对象获得，而相应的释放资源就在该对象的析构函数中,资源不限于内存资源，包括file handles, mutexes, database connections, transactions等。

智能指针就是基于RAII的思想实现的。

### 智能指针

> Smart pointers are used to make sure that an object is deleted if it is no longer used (referenced).

智能指针位于<memory>头文件中，从某种意义上来说，不是一个真的指针，但是重载了 `->` `*` `->*`指针运算符，这使得其表现的像个内建的指针，有以下几种类型：`auto_ptr`, `shared_ptr`,` weak_ptr`,`unique_ptr`。后三个是c++11支持的，第一个已经被弃用了,相应的在boost中也有智能指针，不过现在c++11已经支持了就不用了，比如`boost:scoped_ptr` 类似于 `std:unique_ptr`。

**unique_ptr**

可用在有限作用域（restricted scope）中动态分配的对象资源。不可复制（copy），但是可以转移（move），转移之后原来的指针无效。

```cpp
#include <iostream>
#include <memory>
#include <utility>
 
int main()
{
    std::unique_ptr<int> valuePtr(new int(15));
    std::unique_ptr<int> valuePtrNow(std::move(valuePtr));
}
```

**shared_ptr**

可以复制，维护一个引用计数，当最后一个引用该对象的引用退出，那么才销毁；通常用在私有（private）的类成员变量上，外部通过成员函数获取该成员的引用，如下：

```cpp
#include <memory>
 
class Foo
{
	public void doSomething();
};
 
class Bar
{
private:
	std::shared_ptr<Foo> pFoo;
public:
	Bar()
	{
		pFoo = std::shared_ptr<Foo>(new Foo());
	}
 
	std::shared_ptr<Foo> getFoo()
	{
		return pFoo;
	}
};
```

但是可能带来的问题是 : 

1. 悬垂引用（dangling reference）

``` cpp
// Create the smart pointer on the heap
MyObjectPtr* pp = new MyObjectPtr(new MyObject())
// Hmm, we forgot to destroy the smart pointer,
// because of that, the object is never destroyed!
```

2. 循环引用（circular reference）

``` cpp
struct Owner {
   boost::shared_ptr<Owner> other;
};

boost::shared_ptr<Owner> p1 (new Owner());
boost::shared_ptr<Owner> p2 (new Owner());
p1->other = p2; // p1 references p2
p2->other = p1; // p2 references p1
```

**weak_pointer**

配合`shared_ptr`使用，可避免循环引用的问题。`weak_ptr`不拥有对象，当其需要访问对象时，必须先临时转换成`shared_ptr`，然后再访问，如下例：

```cpp
#include <iostream>
#include <memory>
 
std::weak_ptr<int> gw;
 
void observe()
{
    std::cout << "use_count == " << gw.use_count() << ": ";
    if (auto spt = gw.lock()) {
     // Has to be copied into a shared_ptr before usage
      std::cout << *spt << "\n";
    }
    else {
        std::cout << "gw is expired\n";
    }
}
 
int main()
{
    {
        auto sp = std::make_shared<int>(42);
				gw = sp;
 
				observe();
    }
 
    observe();
}
```

每一个shared_ptr对象内部，拥有两个指针ref_ptr与res_ptr，一个指向引用计数对象，一个指向实际的资源。在shared_ptr的拷贝构造等需要创造出其他拥有相同资源的shared_ptr对象时，会首先增加引用计数，然后将ref_ptr与res_ptr复值给新对象。发生析构时，减小引用计数，查看是否为0，如果是，则释放res_ptr与ref_ptr。
weak_ptr的引入，我认为是智能指针概念的一个补全。一个裸指针有两种类型：一是管理资源的句柄（**拥有对象**），一是指向一个资源的指针（**不拥有对象**）。举个例子，一般我们创建一个对象，在使用完之后销毁，那这个指针是拥有那个对象的，指针的作用域就是这个对象的生命周期，这个指针就是第一类指针。我们在使用观察者（observer）模式时，被监测对象经常会持有所有observer的指针，以便在有更新时去通知他们，但是他并不拥有那些对象，这类指针就是第二类指针。在引入smart_ptr之前，资源的创建与释放都是调用者来做决定，所以一个指针是哪一类，完全由程序员自己控制。

但是智能指针引入之后，这个概念就凸显出来。试想，在上述例子中，我们不会容许一个observer对象因为他是某一个对象的观察者就无法被释放。weak_ptr就是第二类指针的实现，他不拥有资源，当需要时，他可以通过lock获得资源的短期使用权。

总之，`weak_ptr`是对裸指针的使用中的不拥有对象的这类场景进行模拟，当需要访问的时候借助升级为shared_ptr并lock进行访问。