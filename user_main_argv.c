/* user_main_argv.c —— 面向"需要 POSIX argv"的程序的入口桥接（3P6-1）。
 *
 * ## 与 user_main.c 的区别（关键，两者不能混用）
 *
 * BORUIX 的入口 ABI（docs/abi/syscall-abi.md §4）里 `argc` 恒为 0 或 1，`argv[0]` 指向
 * **整条命令行字符串**；经 shell 派生时 shell 已剥掉程序名。即"内核不拆词，拆词是用户
 * 程序的职责"。多数 BORUIX 原生 C 程序自己拆词；但**第三方程序**（如 tcc）假定标准 POSIX
 * argv 数组、逐个解析 argv[1..] 的选项——它们在本 ABI 下会"看不到任何参数"
 * （实测：tcc 收到 argc=1 后选项循环一次都不执行，静默退出 0、无输出）。
 *
 * 本对象为这类程序把命令行拆成真正的 argv 数组。
 *
 * ## 为什么单列一个对象而不改 user_main.c
 *
 * 改 user_main.c 等于**静默改变所有既有 C 程序的 argv 语义**（它们已按"argv[0] 是整条
 * 命令行"写好）。单列对象让两类程序各取所需，链接时**显式选择**。
 *
 * ## 已知边界（S39）
 *
 * - 只按空白（空格/制表符）拆词，**不处理引号与转义**：BORUIX 的命令行引号语义尚未定义，
 *   等 shell 定了再在此处**单点**跟进（不要在别处再写一套拆词）。
 * - 词数上限 64，超出即停止（不越界、不静默截断成错误内容）。
 */

extern int main(int argc, char **argv);
extern void __boruix_init_environ(long argc, const char *const *argv);

#define BX_MAX_ARGS 64
#define BX_LINE_MAX 4096

/* 从入口参数块取可执行文件名（auxv 的 AT_EXECFN）。
 *
 * 布局（ABI §4）：[argc] argv[0..argc-1] [NULL] envp... [NULL] (type,val)... [AT_NULL,0]
 *
 * **全程有界扫描**（S02）：布局异常时退回占位名，绝不越界读。
 * 为什么要真名：argv[0] 按 POSIX 应是程序名，第三方程序会用它（打印用法、重定位自身路径）。
 * 拿不到时用 "prog" 兜底，而不是把某个参数冒充成程序名。 */
static const char *bx_exec_name(const char *const *argv, long argc) {
    const char *const *p;
    long guard;

    if (argc < 0 || !argv) {
        return "prog";
    }
    p = argv + argc + 1;              /* 跳过 argc 个 argv 项与其 NULL 终结槽 */
    for (guard = 0; guard < 4096 && *p; guard++) {
        p++;                          /* 跳过 envp */
    }
    if (guard >= 4096) {
        return "prog";
    }
    p++;                              /* 跳过 envp 的 NULL 终结槽 */
    for (guard = 0; guard < 256; guard++, p += 2) {
        if (p[0] == 0) {
            break;                    /* AT_NULL */
        }
        if (p[0] == 15) {             /* AT_EXECFN */
            return (const char *)p[1];
        }
    }
    return "prog";
}

int user_main(long argc, const char *const *argv) {
    static char line[BX_LINE_MAX];
    static char *av[BX_MAX_ARGS + 2];
    const char *s;
    long i = 0;
    int n = 1;

    /* 环境注册与 user_main.c 同一单点（getenv/environ 才可用）。 */
    __boruix_init_environ(argc, argv);

    /* 命令行 = argv[0]（shell 已剥程序名）；无命令行时为空串。 */
    s = (argc >= 1 && argv && argv[0]) ? argv[0] : "";
    /* 拷进可写缓冲：下面要就地插入 NUL 作为词分隔。 */
    for (i = 0; i < (long)sizeof(line) - 1 && s[i]; i++) {
        line[i] = s[i];
    }
    line[i] = 0;

    av[0] = (char *)bx_exec_name(argv, argc);
    i = 0;
    while (line[i] && n < BX_MAX_ARGS) {
        while (line[i] == ' ' || line[i] == '\t') {
            i++;
        }
        if (!line[i]) {
            break;
        }
        av[n++] = &line[i];
        while (line[i] && line[i] != ' ' && line[i] != '\t') {
            i++;
        }
        if (line[i]) {
            line[i++] = 0;
        }
    }
    av[n] = 0;

    return main(n, av);
}