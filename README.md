# csrc

BORUIX 的独立式 C 运行环境：让普通 C 程序在这个系统上编译、运行的最小运行时。

[English](README.en.md)

不依赖任何外部 C 库：没有完整的标准库、没有文件流，只提供程序启动、系统调用、线程与同步、
极简格式化输出。全部经系统调用与内核直接通信。

## 内容

- `crt0.S`——程序入口 `_start`，设置环境后调用 `main`
- `crtrt.c`——系统调用封装、`exit`、字符串输出
- `thread.c`——内存映射、线程创建/汇合/退出、内核同步字
- `pthread.c`、`pthread.h`——线程生命周期接口
- `pthread_sync.c`——互斥锁、条件变量、信号量
- `stdio.c`——极简格式化输出（`%s %c %d %u %x`）
- `boruix.h`——公开声明
- `linker.ld`——用户态段布局
- `build_c.py`——把单个 C 程序编译链接成可执行文件
- `prog/`——示例程序

## 系统调用约定

系统调用经 `int 0x80` 发出，寄存器分配与内核一致。错误以负数 errno 返回，调用方按小于零判错——
与 C 标准库的全局 `errno` 不同，这是有意的简化。

## 线程与同步

提供的是 POSIX 风格的子集，不是完整的 pthread 实现，几处边界如下：

- 线程返回值只支持整数退出码，经运行时的线程描述符传递，不经内核
- `pthread_join` 只能由组长线程调用，这是内核回收机制的限制
- `pthread_attr_t` 被忽略，不支持自定义线程属性
- 同步对象是完整类型，可声明为全局或栈变量；阻塞与唤醒由内核完成

## 示例程序

`prog/` 下的程序既是功能验证也是用法示例：

- `chelldemo.c`——最小的 C 程序：入口、参数、输出、退出
- `pthreaddemo.c`——创建多个线程、各自返回唯一值、主线程汇合校验
- `pthread_syncdemo.c`——互斥锁、条件变量、信号量
- `pthread_bench.c`——递归创建与汇合的基准
- `forkdemo.c`——`fork()` 与 `waitpid()`

## 构建

```bash
python build_c.py <程序名> <源码目录> <输出目录> [额外的运行时 .c 文件...]
python build_c.py chelldemo prog _build
```

需要 clang（交叉编译目标 `x86_64-unknown-none`）与 LLD 链接器。脚本运行时探测可用的 LLD 并打印
最终选择，链接器在非默认位置也可用。

## 文件结构

```
csrc/
├── crt0.S、crtrt.c      # 入口与运行时
├── thread.c、pthread.c、pthread_sync.c
├── stdio.c、boruix.h、linker.ld
├── build_c.py           # 构建脚本
└── prog/                # 示例程序
```

## 相关项目

- [`libsys`](https://github.com/BRX-Boruix/libsys) —— Rust 侧的系统调用封装，与本仓库相互独立
- [`selftest`](https://github.com/BRX-Boruix/selftest) —— 拉起示例程序做回归

## 许可

MIT License，版权归 Yang Borui 所有。详见 [LICENSE](LICENSE)。
