// Simple shell for MyOS
// Compile: clang --target=aarch64-linux-gnu -ffreestanding -nostdlib -c shell.c -o shell.o
// Link:   aarch64-linux-gnu-ld -N -T shell.ld shell.o -o shell.elf

// Linux ARM64 系统调用号
#define SYS_READ      63
#define SYS_WRITE     64
#define SYS_EXIT      93
#define SYS_OPENAT    56
#define SYS_CLOSE     57
#define SYS_LSEEK     62
#define SYS_BRK       214
#define SYS_MMAP      222
#define SYS_MUNMAP    215
#define SYS_SCHED_YIELD 124
#define SYS_FORK      220
#define SYS_WAIT4     260
#define SYS_KILL      129
#define SYS_EXECVE    221
#define SYS_GETPID    172
#define SYS_GETPPID   173
#define SYS_CHDIR     49
#define SYS_MKDIRAT   34
#define SYS_UNLINKAT  35
#define SYS_DUP3      24
#define SYS_GETDENTS64 217
#define SYS_GETCWD    17
#define SYS_GETNAME   1000

static long syscall6(long n, long a0, long a1, long a2, long a3, long a4, long a5) {
    register long x8 asm("x8") = n;
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x5 asm("x5") = a5;
    asm volatile("svc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5)
                          : "r"(x8) : "memory");
    return x0;
}

static long syscall3(long n, long a0, long a1, long a2) {
    register long x8 asm("x8") = n;
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

#define write(fd, buf, len)  syscall3(SYS_WRITE, fd, (long)(buf), len)
#define read(buf, len)       syscall3(SYS_READ, 0, (long)(buf), len)
#define getpid()             syscall3(SYS_GETPID, 0, 0, 0)
#define exit()               syscall3(SYS_EXIT, 0, 0, 0)
#define fork()               syscall3(SYS_FORK, 0, 0, 0)
#define wait4(pid, wstatus, opts) syscall3(SYS_WAIT4, (long)(pid), (long)(wstatus), (long)(opts))
#define execve(path, argv, envp) syscall3(SYS_EXECVE, (long)(path), (long)(argv), (long)(envp))
#define chdir(path)          syscall3(SYS_CHDIR, (long)(path), 0, 0)
#define getdents64(fd, buf, max) syscall3(SYS_GETDENTS64, (fd), (long)(buf), (max))
#define getcwd(buf, max)     syscall3(SYS_GETCWD, (long)(buf), max, 0)
#define getname(buf, max)    syscall3(SYS_GETNAME, (long)(buf), max, 0)
#define mkdirat(path, mode)  syscall3(SYS_MKDIRAT, -100, (long)(path), (long)(mode))
#define unlinkat(path)       syscall3(SYS_UNLINKAT, -100, (long)(path), 0)
#define touch(path)          syscall3(SYS_OPENAT, -100, (long)(path), 0x242) // O_CREAT|O_RDWR|O_TRUNC
#define open_rdonly(path)    syscall3(SYS_OPENAT, -100, (long)(path), 0)
#define open_wr(path)        syscall3(SYS_OPENAT, -100, (long)(path), 0x241) // O_CREAT|O_WRONLY|O_TRUNC
#define dup3(old, new)       syscall3(SYS_DUP3, (long)(old), (long)(new), 0)

static void puts(const char *s) {
    long len = 0;
    while (s[len]) len++;
    write(1, s, len);
}

static int strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 1;
        if (a[i] == 0) return 0;
    }
    return 0;
}

// 打印无符号十进制数
static void putn(long n) {
    char tmp[24];
    int i = 0;
    if (n == 0) { tmp[i++] = '0'; }
    else {
        while (n > 0) { tmp[i++] = '0' + (n % 10); n /= 10; }
    }
    // 反转输出
    for (int j = i - 1; j >= 0; j--) write(1, &tmp[j], 1);
}

void _start(void) {
    char buf[256];

    puts("Shell v0.3\n");

    // Fork once at the beginning
    long pid = fork();
    if (pid == 0) {
        // Child: shell command loop
        while (1) {
            puts("myos:~$ ");

            int n = read(buf, 240);
            if (n <= 0) continue;

            // Strip trailing newline if present
            if (buf[n-1] == '\n') buf[--n] = 0;

            if (n == 0) continue;

            // echo command
            if (strncmp(buf, "echo", 4) == 0) {
                // 检查 > 重定向
                char *echo_out = 0;
                char *echo_end = buf + n;
                for (char *p = buf + 5; p < echo_end; p++) {
                    if (*p == '>') {
                        *p = 0;
                        echo_out = p + 1;
                        while (*echo_out == ' ') echo_out++;
                        break;
                    }
                }
                long echo_fd = 1;
                if (echo_out && *echo_out) {
                    long fd = open_wr(echo_out);
                    if (fd >= 0) { echo_fd = fd; }
                }
                if (echo_fd == 1) {
                    buf[n] = '\n';
                    write(1, buf + 5, n - 4);
                } else {
                    // echo 内容去掉末尾空格并输出到文件
                    long len = 0;
                    for (char *p = buf + 5; *p; p++) len++;
                    char tmp[256];
                    long ti = 0;
                    for (char *p = buf + 5; *p; p++) tmp[ti++] = *p;
                    tmp[ti] = '\n';
                    syscall3(SYS_WRITE, echo_fd, (long)tmp, ti + 1);
                    syscall3(SYS_CLOSE, echo_fd, 0, 0);
                }
                continue;
            }

            // ps command
            if (strncmp(buf, "ps", 2) == 0) {
                char name_buf[64];
                long pid = getpid();
                long nlen = getname(name_buf, 64);
                puts("PID: ");
                putn(pid);
                puts("  NAME: ");
                if (nlen > 0) { name_buf[nlen] = '\n'; write(1, name_buf, nlen + 1); }
                else { puts("unknown\n"); }
                continue;
            }

            // pwd command
            if (strncmp(buf, "pwd", 3) == 0) {
                char cwd_buf[256];
                long len = getcwd(cwd_buf, 256);
                if (len > 0) {
                    cwd_buf[len] = '\n';
                    write(1, cwd_buf, len + 1);
                }
                continue;
            }

            // cd command
            if (strncmp(buf, "cd", 2) == 0) {
                char *path = buf + 3;
                while (*path == ' ') path++;
                long ret = chdir(path);
                if (ret < 0) {
                    puts("cd: ");
                    puts(path);
                    puts(": not found\n");
                }
                continue;
            }

            // ls command
            if (strncmp(buf, "ls", 2) == 0) {
                char *path = buf + 3;
                while (*path == ' ') path++;
                // If no path given, use NULL (cwd)
                char *dir_path = (*path) ? path : (char*)0;
                // Buffer for directory entries (max 2KB)
                char dent_buf[2048];
                long n = getdents64(0, dent_buf, 2048);
                if (n <= 0) {
                    puts("ls: error\n");
                    continue;
                }
                // linux_dirent64: d_ino(8)+d_off(8)+d_reclen(2)+d_type(1)+d_name(null-terminated)
                long pos = 0;
                while (pos < n) {
                    unsigned short reclen = *(unsigned short*)(dent_buf + pos + 16);
                    if (reclen < 19 || pos + reclen > n) break;
                    unsigned char d_type = *(unsigned char*)(dent_buf + pos + 18);
                    char *name = dent_buf + pos + 19;
                    long name_len = 0;
                    while (name[name_len] && name_len < 256) name_len++;
                    write(1, name, name_len);
                    // 目录加 / 后缀
                    if (d_type == 4) write(1, "/\n", 2);
                    else write(1, "\n", 1);
                    pos += reclen;
                }
                continue;
            }

            // cat command
            if (strncmp(buf, "cat", 3) == 0) {
                char *path = buf + 4;
                while (*path == ' ') path++;
                if (*path == 0) { puts("cat: missing operand\n"); continue; }
                long fd = syscall3(SYS_OPENAT, -100, (long)path, 0);
                if (fd < 0) { puts("cat: failed\n"); continue; }
                char catbuf[256];
                long n;
                while ((n = syscall3(SYS_READ, fd, (long)catbuf, 256)) > 0) {
                    write(1, catbuf, n);
                }
                if (n < 0) { puts("\ncat: read error\n"); }
                syscall3(SYS_CLOSE, fd, 0, 0);
                continue;
            }

            // touch command
            if (strncmp(buf, "touch", 5) == 0) {
                char *path = buf + 6;
                while (*path == ' ') path++;
                if (*path == 0) { puts("touch: missing operand\n"); continue; }
                long fd = touch(path);
                if (fd >= 0) { syscall3(SYS_CLOSE, fd, 0, 0); }
                else { puts("touch: failed\n"); }
                continue;
            }

            // mkdir command
            if (strncmp(buf, "mkdir", 5) == 0) {
                char *path = buf + 6;
                while (*path == ' ') path++;
                if (*path == 0) { puts("mkdir: missing operand\n"); continue; }
                long ret = mkdirat(path, 0777);
                if (ret < 0) { puts("mkdir: failed\n"); }
                continue;
            }

            // rm command
            if (strncmp(buf, "rm", 2) == 0) {
                char *path = buf + 3;
                while (*path == ' ') path++;
                if (*path == 0) { puts("rm: missing operand\n"); continue; }
                long ret = unlinkat(path);
                if (ret < 0) { puts("rm: failed\n"); }
                continue;
            }

            // External command: fork + execve
            {
                // 查找 > 重定向
                char *redirect = 0;
                int i;
                for (i = 0; buf[i]; i++) {
                    if (buf[i] == '>') {
                        buf[i] = 0;
                        redirect = buf + i + 1;
                        while (*redirect == ' ') redirect++;
                        break;
                    }
                }

                long child = fork();
                if (child == 0) {
                    // 重定向输出
                    if (redirect && *redirect) {
                        long fd = open_wr(redirect);
                        if (fd >= 0) {
                            syscall3(SYS_CLOSE, 1, 0, 0);
                            dup3(fd, 1);
                            syscall3(SYS_CLOSE, fd, 0, 0);
                        }
                    }
                    long ret = execve(buf, 0, 0);
                    puts("command not found\n");
                    exit();
                } else if (child > 0) {
                    wait4(child, 0, 0);
                }
            }
            continue;
        }
    } else if (pid > 0) {
        // Parent: wait forever for child
        while (1) {
            wait4(-1, 0, 0);
        }
    }
}
