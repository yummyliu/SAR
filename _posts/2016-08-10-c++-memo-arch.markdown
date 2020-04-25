---
layout: post
title: C++内存管理
date: 2016-08-10 15:58
header-img: "img/head.jpg"
categories: 
    - C++
typora-root-url: ../../layamon.github.io
---

* TOC
{:toc}
了解C++运行时的内存管理，能深入的了解你的程序实际的操作，是定位问题所在以及进行调优基本。本文作者根据个人的了解简述c++运行时的内存布局，以及一些需要了解的点。

# C++内存布局

从如下一个简单的代码开始分析。

```c
#include <stdio.h> 
int main(void) 
{ 
	return 0; 
}
```

通过`size`命令，我们知道文件的布局如下所示，分为三部分：text/data/bss。而当代码运行起来，这三部分就是进程所占地址空间的全局区域，另外加上栈和堆就是一个应用进程的整体布局。下面就分别从这几部分进行梳理。

![image-20200210155950568](/image/cpp-memo/20200210-proglayout.png)

> 这里只是概念图，而且是虚拟地址空间，地址由下向上变大；stack是向下增长，在stack之上是kernelspace；
>
> 实际上heap，stack的起始地址是随机的，因为一旦地址固定，容易被被不法人员利用；
>
> 另外在Free部分，会包含mmap的地址空间，起始地址同样是随机的。

**data & bss**

> bss: block started by symbol

bss保存的是没初始化的全局变量，这段内存是匿名的；

data是初始化的全局变量，这段不是匿名的，与binary文件的相应位置对应，但是是private mmap，因此对这段空间的修改不会flush到二进制中。

**text（代码段）**

text就是代码段。对于代码段，我们需要了解代码是如何从code到机器指令的？如[下图](https://www3.ntu.edu.sg/home/ehchua/programming/cpp/gcc_make.html)所示，中间有4个步骤：

1. Preprocessor：宏展开，引用头文件；
2. Compiler：源代码转成汇编代码；
3. Assembler：将汇编代码转成二进制代码文件；
4. Linker：将多个目标文件和库文件进行链接，生成一整个可执行文件（注意这里的库文件分为静态库和动态库）。

![img](../image/cpp-memo/GCC_CompilationProcess.png)

其中在第4步中，如果是静态库那么每个可执行文件中都会有一个静态库的代码拷贝。这样最终的可执行文件就会比较臃肿；因此，提出了另一个动态（共享）库的链接方式，基于动态库的链接并不会将库代码整合到可执行文件中，而是在可执行文件执行的时候，如果调用了动态库，那么才进行加载。这样可执行文件比较小，并且可以动态更新模块，更加灵活；但是由于具体调用的时候需要对函数入口进行**相对寻址**，这样效率上比静态库会慢一些；而且动态库是在系统中单独存放，存在被人误删等异常操作，从而影响可执行文件的稳定。

**动态连接库的系统共享**

在Linux中，比如libc.so就是一个常用的动态库，该动态库被系统大多数进程共享使用。在链接过程中，可执行文件中会创建一个**符号映射表**。在app执行的时候，OS将控制权先给`ld.so`，而不是先给app。`ld.so`会找到并引入的lib；然后将控制权转给app。通过以下命令，可看到ld搜索库文件的路径。

```bash
# ld --verbose | grep SEARCH_DIR | tr -s ' ;' \\012
SEARCH_DIR("=/usr/x86_64-redhat-linux/lib64")
SEARCH_DIR("=/usr/lib64")
SEARCH_DIR("=/usr/local/lib64")
SEARCH_DIR("=/lib64")
SEARCH_DIR("=/usr/x86_64-redhat-linux/lib")
SEARCH_DIR("=/usr/local/lib")
SEARCH_DIR("=/lib")
SEARCH_DIR("=/usr/lib")
```

> 在写MakeFile的时候，常常在文件中定义了`LDFLAGS`变量(描述定义了使用了哪些库，以及相应的参数)。但是最后连接的时候，将`*.o`和`LDFLAGS`进行连接，出现undefine-reference问题，因为库中明明有这些符号，但是却没有找到，奇怪了，原因如下：
>
> **在UNIX类型的系统中，编译连接器，当命令行指定了多个目标文件，连接时按照自左向右的顺序来搜索外部函数的定义。也就是说，当所有调用这个函数的目标文件名列出后，再出现包含这个函数定义的目标文件或库文件。否则，就会出现找不到函数的错误，连接是必须将库文件放在引用它的所有目标文件之后**

对于动态库的全局代码段，每个进程维护通过相对寻址来执行。而对于动态库中的非常量全局变量不是共享的，每个进程一个拷贝。

** stack**

栈内的变量一般就是在函数内部声明的，或者是函数的形参。其作用域也是在**本次调用**内部可见。但是对于static变量，那么就是多次调用都可见。

** heap**

heap一般就是动态申请的空间的位置。一般有两种动态空间申请的方法：new/malloc。

| NEW                              | MALLOC                   |
| -------------------------------- | ------------------------ |
| 调用构造函数                     | 不                       |
| 是个操作符                       | 是个函数                 |
| 返回确定的数据类型               | 返回void*                |
| 失败抛出异常，也可`new(nothrow)` | On failure, returns NULL |
| 从free store申请空间             | 从heap申请空间           |
| 编译器计算大小                   | 手动设置大小             |

> Free store和heap的区别就是free store上的空间预审请，迟释放。

**delete和 free()一定要和new/malloc()对应使用。**

C++的初始化和赋值是两码事，类对象的引用类型以及const类型需要在定义的时候初始化，对象的赋值一般会调用拷贝构造函数。

在构造函数中来对成员变量进行初始化，如果希望提高初始化的速度可以采用初始化列表（自定义组合类型初始化，减少了一次默认构造函数的调用），但是初始化的顺序依然还是按照**类中出现的先后顺序**进行初始化。另外，C++类中的static类型可看做是全局静态变量，访问需要加上类域。

和普通(成员)函数重载类似，只要参数不同，构造函数都可重载，是C++多态特性的一部分。

> 但是普通(成员)函数有返回值，对于**只有返回值不同其他都相同的函数不能重载**；因为在C++最后编译的函数符号有函数名和参数拼接而成，类似这样foo_int_int_；参数不同可以区分，但是返回值不同就区分不了了。

值得注意的是，如果类的成员变量有和资源相关的类型，比如堆内存、文件句柄等；默认的拷贝构造函数只是提供类似memcpy的按位进行拷贝，通常称为浅拷贝（shallow copy），这时需要自己实现拷贝构造函数实现深拷贝。值得注意的是，这类拷贝构造函数一般比较重，对于一些场景可能有性能问题，比如作为函数返回值。在c++11中，引入了右值引用，同时类也有默认的移动构造函数。

# 右值引用(C++11)

## **std::move**

移动构造函数是相对于拷贝构造函数而言的，在C++11标准中添加的新的构造函数类型。其参数为一个**右值引用**类型，要说明移动构造函数，首先我们来了解一下C++11中对值类型的区分。

![image-20200210101826672](/Users/liuyangming/layamon.github.io/image/cpp-memo/20200210-c++11value.png)

> 关于左值和右值具体的判断很难归纳，就算归纳了也需要大量的解释。

在C++中，广泛被认可的说法是可以取地址的、有名字的就是左值；不能取地址、没有名字的就是右值。由于右值通常没有名字，在C++11中，通过**右值引用**来找到他的存在，区别于常规引用（左值引用），用`&&`来标识，如下：

```c++
T && a = returnRvalue();
```

右值引用和左值引用都属于引用，只是一个别名，其不拥有绑定对象的内存，不存在拷贝的开销；只不过一个具名对象的别名，一个时匿名对象的别名。

使用右值引用的好处是将本将要消亡（返回值在函数结束后，过了自己的生命期，这就是一种**亡值**）的变量重获新生，并且相比于`T a = returnRvalue();`，少了重新析构与构造的代价。

值得注意的是，右值引用是C++11标准的；在C++98中，左值引用无法引用右值的。而**常量左值引用**是一个万能的引用类型，如下编译是没有问题的，但是之后该变量只能是只读的。

```c++
const T & a = returnRvalue();
```

这样在C++98中，通常我们使用常量引用类型作为函数参数，可以避免函数传递右值时的析构构造代价，如下：

```c++
void func(const T & a){
	// do something
}
func(returnRvalue());
```

在C++11中，可以直接使用右值引用作为参数：

```c++
void func(T && a){
	// do something
}
func(returnRvalue());
```

在C++11中，标准库<utility>中提供了一个函数`std::move`，该函数可认为是一个强制类型转换，将左值转换为右值。move结合移动构造函数可以应用在即将消亡的大对象的移动中，如下：

```c++
class BigObj1 {
public:
  BigObj1(BigObj1 && b1):
  c(b1.c)
  {
    b1.c = nullptr;
  }
  
  int* c;
}
class BigObj2 {
  // ...
}

class ResouceManager {
public:
  ResourceManager(ResourceManager && r):
  b1(std::move(r.b1)),
  b2(std::move(r.b2))
  {
    // ...
  }
  BigObj1 b1;
  BigObj2 b2;
}

ResouceManager gettemp() {
  ResouceManger tmp;
  // ...
  return tmp;
}

int main() {
  ResourceManager r(gettemp());
}
```

在gettemp返回时，本来即将消亡了ResourceManager通过ResourceManager的移动构造函数转移给了新的ResourceManager对象，而原ResourceManager中的成员变量通过std::move强制转换为右值引用，同样通过相应的移动构造函数进行了移动。**这样通过std::move保证了移动语义向成员变量的传递**。

因此，在编写类的移动构造函数时，注意使用std::move来转换资源类型的成员变量，比如堆内存，文件句柄等。

在C++11中，会有默认的拷贝构造函数和移动构造函数，如果需要自己实现的话，拷贝构造函数和移动构造函数必须要一起提供，否则就只有一种语义。一般来说，很少有类只有一种语义，一般只有移动类语义的都是一些资源类的对象，比如智能指针里的`unique_ptr`。

另外，为保证移动的过程中不会因为抛出异常而中断，可在移动构造上加一个`noexcept`关键字，并使用std::move_if_noexcept，当有except时，降级为拷贝构造函数，这是一种牺牲性能保证安全的做法。

> 值得注意的是，编译器会默认进行返回值的优化（Return Value Optimization），可以通过`-fno-elide-constructors`关闭这个优化。

## **std::forward**

用在函数模板中，实现转发语义。具体实现上和move类似，起个不同的名字用在不同的场景中，方便以后扩展。

> **引用折叠**
>
> 在C++11中，引入了一个右值引用，那么在传递参数的时候，会面临引用重叠的问题，比如以下例子在c++98中无法编译通过：
>
> ```c++
> typedef const int T;
> typedef T& TR;
> TR& v = 1; // compiling error
> ```
>
> 在c++11中，通过引入了引用折叠规则，避免这个问题；总结来说，当引用重叠时，只要其中有左值引用，那么优先转换为左值引用

在函数模板的推导中，如果实参为左值引用，那么就推导为一个左值引用参数的函数；如果实参是右值引用，那么就转发为一个右值引用的函数。因此，对于一个转发函数模板，我们将参数定义为右值引用类型，这样左值和右值传递都没有问题。

# RAII

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

**"RAII: Resource Acquisition Is Initialization"**
意思就是任何资源的获取，不管是不是在初始化阶段，都是被一个对象获得，而相应的释放资源就在该对象的析构函数中,资源不限于内存资源，包括file handles, mutexes, database connections, transactions等。

智能指针就是基于RAII的思想实现的。

> Smart pointers are used to make sure that an object is deleted if it is no longer used (referenced).

智能指针位于<memory>头文件中，从某种意义上来说，不是一个真的指针，但是重载了 `->` `*` `->*`指针运算符，这使得其表现的像个内建的指针，有以下几种类型：`auto_ptr`, `shared_ptr`,` weak_ptr`,`unique_ptr`。后三个是c++11支持的，第一个已经被弃用了,相应的在boost中也有智能指针，不过现在c++11已经支持了就不用了，比如`boost:scoped_ptr` 类似于 `std:unique_ptr`。

## **unique_ptr**

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

## **shared_ptr**

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

## **weak_ptr**

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

每一个shared_ptr对象内部，拥有两个指针ref_ptr与res_ptr，一个指向引用计数对象，一个指向实际的资源。在shared_ptr的拷贝构造等需要创造出其他拥有相同资源的shared_ptr对象时，会首先增加引用计数，然后将ref_ptr与res_ptr赋值给新对象。发生析构时，减小引用计数，查看是否为0，如果是，则释放res_ptr与ref_ptr。
weak_ptr的引入，我认为是智能指针概念的一个补全。一个裸指针有两种类型：

+ 管理资源的句柄（**拥有对象**），举个🌰，一般我们创建一个对象，在使用完之后销毁，那这个指针是拥有那个对象的，指针的作用域就是这个对象的生命周期，这个指针就是用来管理资源的句柄。
+ 指向一个资源的指针（**不拥有对象**），举个🌰，我们在使用观察者（observer）模式时，被监测对象经常会持有所有observer的指针，以便在有更新时去通知他们，但是他并不拥有那些对象，这类指针就是指向一个资源的指针。

在引入smart_ptr之前，资源的创建与释放都是调用者来做决定，所以一个指针是哪一类，完全由程序员自己控制。但是智能指针引入之后，这个概念就凸显出来。试想，在上述例子中，我们不会容许一个observer对象因为他是某一个对象的观察者就无法被释放。weak_ptr就是第二类指针的实现，他不拥有资源，当需要时，他可以通过lock获得资源的短期使用权。

因此，`weak_ptr`是对裸指针的使用中的不拥有对象的这类场景进行模拟，当需要访问的时候借助升级为shared_ptr并lock进行访问；举个🌰，在连接超时释放的场景中([http://kernelmaker.github.io/TimingWheel])，用一个固定大小的数据维护最近N秒的连接，其中数组元素都是shared_ptr，在连接对象`Conn`中维护了weak_ptr（这里的场景就是不拥有对象，而只是观察对象的状态）；当有了新的请求，那么需要移动Conn的槽位，那么，首先将weak_ptr升级为一个shared_ptr，然后放到相应槽位中；这样在释放旧连接时，如果之前发生过拷贝，那么相应shared_ptr释放的时候，不会释放真正的连接对象。



https://manybutfinite.com/post/anatomy-of-a-program-in-memory/