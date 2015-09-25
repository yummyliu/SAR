---
layout: post
title: stack & register
date: 2021-06-11 11:35
categories:
  - Linux
typora-root-url: ../../layamon.github.io
---



> 虽然上过汇编课，但平时很少关注具体寄存器与汇编指令；一般对gdb的使用，上来直接bt，然后加断点开始调试，这两天借着一个问题，重温了一下stack与register相关的概念，整理此文

我们都知道，在CPU上运行的二级制代码，可看作基于汇编命令来对各个寄存器和Memory进行计算与存储。在目前X86(64)架构中的多种寄存器都是从一开始的16bit寄存器发展而来的，如下图，只是在后来位宽变成32时，在之前加上'**E**'，以及后来又扩展为64位宽时，则加上'**R**'）。寄存器分为general 和 special两类；能对special中的BP（base pointer），SP（stack pointer），IP（instruction pointer）的作用有了解，基本能了解点程序的运行机制。

<img src="/image/assemble/register.png" alt="image-20210611173204022" style="zoom: 33%;" />

目前的CPU不只有上图这些寄存器，还有一些其他寄存器，比如floating point相关等；另外在目前的X86-64架构中，还增加了R8,R9,R10等编号的通用寄存器。

那么，本文基于如下示例代码，用gdb来看一下程序中一次函数调用相关的汇编指令与寄存器。

```c++
struct BigType128 {
  uint64_t a;
  uint64_t b;
};

struct BigType156 {
  uint64_t a;
  uint64_t b;
  uint64_t c;
};

uint64_t foo(uint64_t x, BigType128 a, BigType156 b, uint64_t &c, uint32_t y,
             uint32_t *z, uint64_t l) {
  uint64_t local_var = 1;
  char local_array[10] = "123456789";
  static int static_var = -1;
  static_var += 1;
  static char static_array[] = "asdf";
  a.a++;
  c++;
  l++;
  (*z)++;
  return local_var + static_var;
}

int main() {
  uint64_t c = 0x4444444444444444;
  uint32_t z = 0x66666666;
  auto a = foo(0x1111111111111111, {0x2222222222222222, 0x2222222222222222},
               {0x3333333333333333, 0x3333333333333333, 0x3333333333333333}, c,
               0x55555555, &z, 0x7777777777777777);
  return 0;
}
```

我们在foo函数上加断点，看一下程序运行时相关的寄存器情况：

```assembly
(gdb) i r
rax            0x2222222222222222       2459565876494606882
rbx            0x0      0
rcx            0x7fffffffdf28   140737488346920
rdx            0x2222222222222222       2459565876494606882
rsi            0x2222222222222222       2459565876494606882
rdi            0x1111111111111111       1229782938247303441
rbp            0x7fffffffdef0   0x7fffffffdef0
rsp            0x7fffffffdef0   0x7fffffffdef0
r8             0x55555555       1431655765
r9             0x7fffffffdf24   140737488346916
r10            0x73     115
r11            0x1c     28
r12            0x5555555593d0   93824992252880
r13            0x7fffffffe030   140737488347184
r14            0x0      0
r15            0x0      0
rip            0x555555559525   0x555555559525 <foo(unsigned long, BigType128, BigType156, unsigned long&, unsigned int, unsigned int*, unsigned long)+37>
eflags         0x202    [ IF ]
cs             0x33     51
ss             0x2b     43
ds             0x0      0
es             0x0      0
fs             0x0      0
gs             0x0      0
```

这里我故意设置了7个参数，由于**rdi、rsi、rdx、rcx、r8、r9**会被用来进行参数传递，通常可传递6个参数，但是每个寄存器只有8B，这里我增加了两个大类型，size分别是16和24；可以发现rsi、rdx共同完成16B大小的一个参数的传递；对于引用类型其实就是传递了一个地址；

了解了寄存器的信息，我们还有疑问——有两个参数没有使用寄存器来进行参数传递，去哪了？接着看代码。在看代码之前，先有个基本的认识：CPU通过PC寄存器一条条的读取指令执行，执行的的指令中如果有func call，那么就需要进出栈，栈中都是操作的对象数据；而栈的状态通过两个寄存器保持——rsp（当前的栈顶）与rbp（当前的栈底）。

> 关于mov指令，有两种语法：
>
> `mov dest, src` is called **[Intel syntax](https://stackoverflow.com/tags/intel-syntax/info)**. (e.g. `mov eax, 123`)
>
> `mov src, dest` is called **[AT&T syntax](https://stackoverflow.com/tags/att/info)**. (e.g. `mov $123, %eax`)
>
> UNIX assemblers including the GNU assembler uses AT&T syntax, all other x86 assemblers I know of uses Intel syntax. You can read up on the differences [on wikipedia](http://en.wikipedia.org/wiki/X86_assembly_language#Syntax).
>
> 我这里的gdb，是基于UNIX语法。

那么，看汇编代码，我主要关注了这几个信息：

- pc/rsp/rbp等寄存器的值变化
- 各变量的地址：局部变量与函数参数都通过 rbp+ 偏移的方式访问，注意是向上还是向下加偏移 `(-)0xN (%rbp)`。
  - 其中b和l就是没有通过寄存器传递的参数，他们是提前压栈了；压栈发生在callq之前，观察这两个的访问方式。
- 引用与指针类型的访问方式
  - c 与 z的的汇编代码没有区别，可以看出引用于指针传递性能上没差别，引用写法更友好；但是示例代码中都是对普通变量的引用，这就没有必要了，这时传值性能更高。

```assembly
(gdb) disassemble /s
Dump of assembler code for function foo(unsigned long, BigType128, BigType156, unsigned long&, unsigned int, unsigned int*, unsigned long):
/data00/liuyangming/GoofDB/tools/mytest/mytest.cc:
22                   uint32_t *z, uint64_t l) {
   0x0000555555559500 <+0>:     push   %rbp # 将前一个rbp保存在栈内，rsp+8；
   0x0000555555559501 <+1>:     mov    %rsp,%rbp # 然后设置新的rbp；
   0x0000555555559504 <+4>:     mov    %rdi,-0x28(%rbp) # 第一个参数压栈
   0x0000555555559508 <+8>:     mov    %rsi,%rax # v 以下是第二个参数压栈
   0x000055555555950b <+11>:    mov    %rdx,%rsi
   0x000055555555950e <+14>:    mov    %rsi,%rdx
   0x0000555555559511 <+17>:    mov    %rax,-0x40(%rbp) 
   0x0000555555559515 <+21>:    mov    %rdx,-0x38(%rbp) # ^
   0x0000555555559519 <+25>:    mov    %rcx,-0x30(%rbp) # 第三个
   0x000055555555951d <+29>:    mov    %r8d,-0x44(%rbp) # 第四个
   0x0000555555559521 <+33>:    mov    %r9,-0x50(%rbp) # 第五个

23        uint64_t local_var = 1;
=> 0x0000555555559525 <+37>:    movq   $0x1,-0x8(%rbp) #给出了将要执行的下一条指令，即局部变量赋值1；可以看出局部变量的地址高于函数参数的地址。

24        char local_array[10] = "123456789";
   0x000055555555952d <+45>:    movabs $0x3837363534333231,%rax
   0x0000555555559537 <+55>:    mov    %rax,-0x12(%rbp) # 0x12 = 8 + 10, 可以看出local_array紧挨着local_var存储在栈上。
   0x000055555555953b <+59>:    movw   $0x39,-0xa(%rbp)

25        static int static_var = -1;
26        static_var += 1;
   0x0000555555559541 <+65>:    mov    0x265fe9(%rip),%eax        # 0x5555557bf530 <_ZZ3foom10BigType12810BigType156RmjPjmE10static_var>; 可以看出静态变量的地址与栈无关，位于代码段。
   0x0000555555559547 <+71>:    add    $0x1,%eax
   0x000055555555954a <+74>:    mov    %eax,0x265fe0(%rip)        # 0x5555557bf530 <_ZZ3foom10BigType12810BigType156RmjPjmE10static_var>

27        static char static_array[] = "asdf";
28        a.a++;
   0x0000555555559550 <+80>:    mov    -0x40(%rbp),%rax
   0x0000555555559554 <+84>:    add    $0x1,%rax
   0x0000555555559558 <+88>:    mov    %rax,-0x40(%rbp)

29        c++;
   0x000055555555955c <+92>:    mov    -0x30(%rbp),%rax  # 解引用
   0x0000555555559560 <+96>:    mov    (%rax),%rax
   0x0000555555559563 <+99>:    lea    0x1(%rax),%rdx
   0x0000555555559567 <+103>:   mov    -0x30(%rbp),%rax
   0x000055555555956b <+107>:   mov    %rdx,(%rax)

30        l++;
   0x000055555555956e <+110>:   addq   $0x1,0x28(%rbp) # 访问l是通过rbp 向高地址加偏移的方式访问

31        (*z)++;
   0x0000555555559573 <+115>:   mov    -0x50(%rbp),%rax # 解指针
   0x0000555555559577 <+119>:   mov    (%rax),%eax
   0x0000555555559579 <+121>:   lea    0x1(%rax),%edx
   0x000055555555957c <+124>:   mov    -0x50(%rbp),%rax
   0x0000555555559580 <+128>:   mov    %edx,(%rax)

32        return local_var + static_var;
   0x0000555555559582 <+130>:   mov    0x265fa8(%rip),%eax        # 0x5555557bf530 <_ZZ3foom10BigType12810BigType156RmjPjmE10static_var>
   0x0000555555559588 <+136>:   movslq %eax,%rdx # x86规定 eax寄存器存储返回值
   0x000055555555958b <+139>:   mov    -0x8(%rbp),%rax
   0x000055555555958f <+143>:   add    %rdx,%rax

33      }
   0x0000555555559592 <+146>:   pop    %rbp
   0x0000555555559593 <+147>:   retq   
End of assembler dump.
```

以上，这里只是笔者简单的抛砖引玉，其实有很多疑问还没有解答，比如多个size小于8的参数传递，会不会共用一个寄存器？返回值如果大于8，如何返回？一试便知，如果感兴趣可以继续试验。
