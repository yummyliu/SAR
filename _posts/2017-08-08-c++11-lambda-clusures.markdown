---
layout: post
title: c++11-lambda-clusures
date: 2017-08-08 09:29
header-img: "img/head.jpg"
categories: jekyll update
tags:
    - C++
---

# 0. 前言

c++11，java8 这些老语言的新版本，以及go这些新语言都是加上了一下函数式编程的特性，为啥要有函数式编程？和过程式编程有啥区别？又出现在哪些场景中呢？
函数式编程的

> 关于函数对象和闭包的区别： 对象是附带行为的数据，闭包是附带数据的行为

**函数对象的一个弊端**

创建函数对象太不方便了，必须在另外一个地方定义一个类，然后才能用

``` cpp
#include <iostream>

using namespace std;

int main()
{
    auto func = [] () { cout << "Hello world"; };
    func(); // now call the function
}
```

[] : the capture specification, 告诉编译器在这创建了一个lambda函数
() : the argument list
return value : 并不需要明确指出，c++11中，编译器可以推断出lambda函数的返回值类型，上例中，编译器明白函数返回nothing，
{} : 函数体， 这里并不执行，只是定义。

**NOTE**: auto关键使我们用起来更方便，比起原来重载一个()操作符，来实现函数对象，这样更加灵活方便

## Variable Capture with Lambdas

``` cpp
vector<string> findAddressesFromOrgs ()
{
    return global_address_book.findMatchingAddresses(
        // we're declaring a lambda here; the [] signals the start
        [] (const string& addr) { return addr.find( ".org" ) != string::npos; }
    );
}
```
VS
``` cpp
// read in the name from a user, which we want to search
string name;
cin>> name;
return global_address_book.findMatchingAddresses(
    // notice that the lambda function uses the the variable 'name'
    [&] (const string& addr) { return addr.find( name ) != string::npos; }
);
```

上面是定义了一个函数 找到 key为 "org"的对象，下者可以引用函数外的对象，比起只查找固定的name，这里name是函数外的变量
通过[&]来定义匿名函数，编译器就开始捕获变量。

## Lambda and STL

``` cpp
vector<int> v;
v.push_back( 1 );
v.push_back( 2 );
//...
for ( auto itr = v.begin(), end = v.end(); itr != end; itr++ )
{
    cout << *itr;
}
```
VS
``` cpp
vector<int> v;
v.push_back( 1 );
v.push_back( 2 );
//...
for_each( v.begin(), v.end(), [] (int val)
{
    cout << val;
} );
```

下者的实现方式有更好的代码可读性，并且结构清晰。并且在性能上不会有损失，并且有时候会有优势，因为利用了循环展开的优化,
通过STL的实例，不能简单的认为lambda仅仅是一个创建函数的特性，这是一种新的编程方式。将函数作为参数，将数据访问的方法独立出来

## More of New Lambda Syntax

## return values :

``` cpp
[] () -> int { return 1; } //// now we're telling the compiler what we want
[] () { return 1; } // compiler knows this returns an integer
```

## Throw Specifications

``` cpp
[] () throw () { /* code that you don't expect to throw an exception*/ }
```

# How are Lambda Closures Implemented

本质上就是创建了一个类，实现了()操作符。当时在c++11中，比起另外定义一个函数类，我们可以随着使用这一特性

但是，c++是一个对性能很要求的语言，对于[]，有不同的形式，不同的形式获取不同的变量

+ [] : 并不捕获任何变量，这种方式c++不会创建一个类，而是创建一个普通函数
+ [&] : 通过引用的方式，捕获变量; 注意: 如果从一个函数中返回一个lambda函数，不能用这个方案, 因为变量可能会在函数返回后，失效。
+ [=] : 通过复制的方式引用变量
+ [=, &foo] : 除了foo之外的其他变量，都是通过引用的方式，其他用复制的方式
+ [bar] : 将bar复制一份，不管其他的
+ [this] : 将this只想的整个类，copy一下

# What type is a Lambda

上面是通过auto 来接的Lambda，但是每个Lambda实现方式都是一个单独的类, 这似乎会产生很多类，

但是在C++11中, 实现了一个方便的包装器，存储各种类型的function，包括--lambda function, functor, or function pointer: std::function.
注意当capture是[]的时候，lambda实现为一个函数指针，故下述代码是可行的

``` cpp
typedef int (*func)();
func f = [] () -> int { return 2; };
f();
```
通过std:function<>, 我们可以替代原先的模板，
``` cpp
#include <string>
#include <vector>

class AddressBook
{
    public:
    // using a template allows us to ignore the differences between functors, function pointers
    // and lambda
    template<typename Func>
    std::vector<std::string> findMatchingAddresses (Func func)
    {
        std::vector<std::string> results;
        for ( auto itr = _addresses.begin(), end = _addresses.end(); itr != end; ++itr )
        {
            // call the function passed into findMatchingAddresses and see if it matches
            if ( func( *itr ) )
            {
                results.push_back( *itr );
            }
        }
        return results;
    }

    private:
    std::vector<std::string> _addresses;
};
```
VS
``` cpp
#include <functional>
#include <vector>

class AddressBook
{
    public:
    std::vector<string> findMatchingAddresses (std::function<bool (const string&)> func)
    {
        // check if we have a function (we don't since we didn't provide one)
        if ( func )
        {
            // if we did have a function, call it
                std::vector<string> results;
                for ( auto itr = _addresses.begin(), end = _addresses.end(); itr != end; ++itr )
                {
                    // call the function passed into findMatchingAddresses and see if it matches
                    if ( func( *itr ) )
                    {
                        results.push_back( *itr );
                    }
                }
                return results;
        }
    }
    private:
    std::vector<string> _addresses;
};
```

# Make delegate with lambda

``` cpp
EmailProcessor processor;
MessageSizeStore size_store;
processor.setHandlerFunc( checkMessage ); // this won't work
```
VS
``` cpp
EmailProcessor processor;
MessageSizeStore size_store;
processor.setHandlerFunc(
        [&] (const std::string& message) { size_store.checkMessage( message ); }
);
```

# In the End

通过c++匿名函数，我们可以减少代码量，提升单元测试, 有的时候能够替代一些之前用宏实现的功能

[c++lambda][youtube]
[c++lambda]:http://www.cprogramming.com/c++11/c++11-lambda-closures.html
