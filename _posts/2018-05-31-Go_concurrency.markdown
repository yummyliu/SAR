---
layout: post
title: 
date: 2018-05-31 13:24
header-img: "img/head.jpg"
categories: jekyll update
tags:
   - Go
---

# Go Concurrency Patterns: Context

## Introduction

在Go的servers中，每个到达的请求都被相应的goroutine处理。请求处理这同样会启动额外的请求来访问后端的服务，比如DB或者rpc服务；这些处理同一个request的goroutines集合，往往需要访问该request相关的值，比如终端用户的id，认证token，或者请求的deadline。当一个request被取消或者timeout，所有服务于这个request的goroutines应该立马停止，这样系统才能回收相关的资源。

在Google，我们开发了一个context包，来处理这个问题。它可以很容易的传输该request相关的值，取消的信号，以及dealline信息。

#### Context

context包的核心是Context类型

```go
// A Context carries a deadline, cancelation signal, and request-scoped values
// across API boundaries. Its methods are safe for simultaneous use by multiple
// goroutines.
type Context interface {
    // Done returns a channel that is closed when this Context is canceled
    // or times out.
    Done() <-chan struct{}

    // Err indicates why this context was canceled, after the Done channel
    // is closed.
    Err() error

    // Deadline returns the time when this Context will be canceled, if any.
    Deadline() (deadline time.Time, ok bool)

    // Value returns the value associated with key or nil if none.
    Value(key interface{}) interface{}
}
```

Done方法返回一个channel，作为取消的信号，通知运行中的函数"channel取消啦" ，被通知的函数应该立马取消他们的工作并返回。Err方法返回一个错误，标识为什么Context被取消了。

Context没有取消函数的原因和Done方法的channel只是纯接收的原因相同：接收到取消信号的Function往往不是发送这个信号的。特别是，当一个父操作为某个子操作启动了一个goroutines，这些子操作不能够取消父操作。反而，后面介绍的一个`WithCancel`函数提供了方法，来取消一个新的context；

Context在多个goroutine中同时使用是安全的。代码中，我们可以将一个context传给任意多个goroutines，并且取消这个Context来通知这所有的goroutines；

Deadline方法允许函数自己决定什么时候启动；如果只有很少的时间，这个函数没什么意义。代码中，可以用deadline来设置I/O操作的timeouts；

Value允许Context带有request全局的信息。这些信息必须是多个goroutines并发访问安全的。

#### Derived contexts

context包中提供了一些函数，允许从一个存在的context中派生新的context；这些值构成一个🌲，如果父Context被取消了，那么所有的Context都会被取消；

`Background`是任何context树的根；它永远不会被取消：

```go
// Background returns an empty Context. It is never canceled, has no deadline,
// and has no values. Background is typically used in main, init, and tests,
// and as the top-level Context for incoming requests.
func Background() Context
```

`WithCancel`和`WithTimeout`返回derived Context值，而这些Context会比父Context更早取消。和到来的request相关的Context会在request handler返回的时候取消。`WithCancel`也可以在我们使用多副本的时候，用来取消冗余的请求。`WithTimeout`用来设置到后端server的timeout；

```go
// WithCancel returns a copy of parent whose Done channel is closed as soon as
// parent.Done is closed or cancel is called.
func WithCancel(parent Context) (ctx Context, cancel CancelFunc)

// A CancelFunc cancels a Context.
type CancelFunc func()

// WithTimeout returns a copy of parent whose Done channel is closed as soon as
// parent.Done is closed, cancel is called, or timeout elapses. The new
// Context's Deadline is the sooner of now+timeout and the parent's deadline, if
// any. If the timer is still running, the cancel function releases its
// resources.
func WithTimeout(parent Context, timeout time.Duration) (Context, CancelFunc)
```

`WithValue`提供了一个将request全局值和Context绑定的方法

```go
// WithValue returns a copy of parent whose Value method returns val for key.
func WithValue(parent Context, key interface{}, val interface{}) Context
```



































[go_concurrency_pattern:context](https://blog.golang.org/context)