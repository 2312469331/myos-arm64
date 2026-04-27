Make 的核心思路其实就三件事：
- 有一张“依赖图”（谁依赖谁）
- 知道“目标文件”和“伪目标”的区别
- 会根据文件的“时间戳”决定“要不要重新编译”（增量编译）
下面按你刚才踩坑的顺序讲，顺便解释你 Makefile 里用到的各种写法。
---
## 1. 最容易踩的坑：默认目标（`make` 到底先执行谁）
规则（很硬）：
- 如果你只输入 `make`，它会把 Makefile 里遇到的**第一个非模式、非点开头的“目标”当作默认目标**，然后只去构建这个目标。
- 可以用 `.DEFAULT_GOAL` 显式指定默认目标，这样无论怎么挪规则位置，`make` 都会从你指定的目标开始。
你这次踩坑就是典型的“默认目标被抢走”问题：
- 原先 Makefile 里：`all: ...` 在前面，所以 `make` = `make all`，一切正常。
- 后来我在中间插了一段规则：  
  `$(RUST_LIB): ...` 出现在 `all:` 之前，结果它变成了第一个目标 → `make` 只构建 Rust 库，就结束了。
- 解决办法：在开头写上 `.DEFAULT_GOAL := all`，强制锁定“默认从 all 开始”。
顺便说一句，`all` 这个名字只是“约定俗成”，不是关键字。你也可以叫 `build`、`default`、`dog`，都行，只要它是第一个目标或被 `.DEFAULT_GOAL` 指定即可。
---
## 2. 两种目标：真实文件 vs 伪目标（`.PHONY`）
### 2.1 真实文件（带命令的普通规则）
```makefile
build/kernel/main.o: kernel/main.c
    $(CC) $(CFLAGS) -c $< -o $@
```
含义：
- 这是一个“真实文件”规则：`build/kernel/main.o` 真的是磁盘上的一个文件。
- Make 会：
  - 先检查前置条件是否存在、是否比目标新。
  - 只有当前置条件比目标新，或者目标不存在时，才执行下面的命令。
这也就是你看到的“增量编译”效果：只要 `.c` 没变，`.o` 就不重新生成。
### 2.2 伪目标（`.PHONY`）
```makefile
.PHONY: clean run debug
clean:
    rm -rf build/*
run: all
    $(QEMU) ...
```
含义：
- `clean`、`run`、`debug` 通常并不是真实文件名（除非你故意建一个叫 `clean` 的文件……那就非常坑）。
- 加上 `.PHONY`，就是告诉 Make：
  - “这个目标只是个动作名称，跟文件无关。”
  - 每次被当作“终极目标”或者被别人依赖时，都要无条件执行命令。
### 2.3 那个“只编译 Rust”的坑，和 `.PHONY` 的禁忌
你之前这段写法就是“禁忌”：
```makefile
.PHONY: rust
rust:
    cargo build ...
$(RUST_LIB): rust   # 真实文件依赖伪目标
```
GNU Make 的规则是：
- 如果一个真实文件的**前置条件是 `.PHONY`**，那这个文件会被认为“永远过期”，每次都会被重建。
所以后果就是：
- 所有 `.o` 都很新 → C/汇编全跳过。
- `$(RUST_LIB)` 依赖伪目标 `rust` → 被认为永远要重建 → 每次只看到 Rust 在编译。
- `make debug` 依赖 `all` → `all` 依赖很多 `.o` → 如果你刚 `make clean`，所有 `.o` 都没了，就只能全量编译，所以看起来“正常”。
正确做法就是我们现在用的：
```makefile
$(RUST_LIB):    # 不依赖 .PHONY
    cargo build ...
```
这样 Make 会像对待普通文件一样：只有 `$(RUST_LIB)` 不存在或者 Rust 源码变化，才重新执行 `cargo build`。
---
## 3. 依赖图与重建算法：Make 到底怎么决定要执行什么？
你可以把 Make 想象成一个简单的图算法引擎：
```mermaid
flowchart LR
  G["终极目标 (make/debug/all)"] --> D["依赖 (build/kernel.elf)"]
  D --> O["一堆 .o 和 .a 文件"]
  O --> S["源文件 (.c .S)"]
```
### 3.1 构建过程（你看到的“mkdir + 编译 + 链接”）
大致步骤：
1. 找到“终极目标”：  
   - `make` → 默认目标（通常是 `all`）  
   - `make debug` → `debug`  
2. 展开依赖链：递归解析所有前置条件。
3. 按依赖顺序执行命令：
   - 从“叶子”（源文件）往上走；
   - 如果某个目标比它所有的前置条件都新（或者不存在），就执行它的命令；否则跳过。
你的日志里：
- `make debug` 时先 mkdir，再编译 boot.S、main.c……最后链接 ELF，就是这个顺序。
- 这些命令就是 Makefile 里每个目标下面的“配方（recipe）”。
### 3.2 “配方”执行的小细节（你可能还没注意到的）
- 配方里的每条命令都在一个**独立的子 shell** 里执行。
  - 所以 `cd xxx && cargo build` 很常见；如果只写 `cd xxx`，下一行会回到原目录。
- 行首的 `@` 表示“静默执行”，不打印命令本身：
  - `@mkdir -p $(dir $@)`：执行 mkdir，但不打印这行命令。
- 行首的 `-` 表示“允许失败不中断”：
  - `-rm -f xxx`：哪怕文件不存在也不会让 Make 停止。
---
## 4. 变量、自动变量和常用函数
### 4.1 普通变量
你已经在大量使用：
```makefile
CC      := $(CROSS_COMPILE)clang
CFLAGS  := -Wall -Wextra -O0 ...
```
要点：
- `:=` 是“立即展开”（赋值时就展开里面的变量）。
- `=` 是“延迟展开”（用到时再展开）。
- `+=` 追加内容。
- `?=` 仅在未定义时赋值。
变量在 Makefile 里通常是“全局”的；只要在引用前定义即可（除了一些特殊情况）。
### 4.2 自动变量（每个规则里“现算”的变量）
在规则命令里，这几个最常用：
- `$@`：当前目标（target）
- `$<`：第一个前置条件
- `$^`：所有前置条件（去重）
- `$*`：模式规则中的“茎”（`%` 匹配的部分）
你这段就用了 `$@` 和 `$<`：
```makefile
build/%.o: %.c
    @mkdir -p $(dir $@)
    $(CC) $(CFLAGS) -c $< -o $@
```
解释：
- `$@` = 生成的 `.o` 路径（例如 `build/kernel/main.o`）
- `$<` = 对应的 `.c` 路径（例如 `kernel/main.c`）
### 4.3 常用文本/文件名函数
你在 Makefile 里已经用得不少了：
- `$(patsubst pattern,replacement,text)`：模式替换
  ```makefile
  OBJ = $(patsubst %.c,build/%.o,$(SRC_C))
  ```
  含义：把 `SRC_C` 中的每个 `xxx.c` 换成 `build/xxx.o`。
- `$(dir names)`：取目录部分
  ```makefile
  OBJ_DIRS := $(sort $(dir $(OBJ)))
  ```
- `$(sort list)`：排序去重
- `$(findstring find,in)`：在 in 中查找 find，找到就返回 find，否则空字符串
  ```makefile
  $(if $(findstring arch/arm64/boot/,$<),...)
  ```
- `$(if condition,then-part[,else-part])`：条件判断（你用来区分 boot 段的编译选项）
---
## 5. 模式规则与静态模式规则
### 5.1 模式规则（你 Makefile 里的主力）
```makefile
build/%.o: %.c
    $(CC) $(CFLAGS) -c $< -o $@
```
含义：
- 对于任何 `xxx.c`，都能匹配 `build/xxx.o: xxx.c`。
- Make 会自动按这个模板，为每个 `.c` 生成一条“隐含规则”。
同样你有：
```makefile
build/%.o: %.S
    $(AS) $(ASFLAGS) -c $< -o $@
```
这就是你整个 C/汇编编译都只需要写两行规则就能覆盖所有文件的原因。
### 5.2 静态模式规则（适合按类别批量处理）
如果以后需要，可以写得更明确一点：
```makefile
$(OBJ): build/%.o: %.c
    $(CC) $(CFLAGS) -c $< -o $@
```
这里明确说：`$(OBJ)` 里的每个 `.o` 都按 `build/%.o: %.c` 来匹配。
---
## 6. 环境分支：Termux / Windows / Ubuntu
你这段很好用，顺便帮大家总结一下常见写法：
```makefile
ifeq ($(shell uname -o), Android)
    CROSS_COMPILE := aarch64-linux-android-
    CC := $(CROSS_COMPILE)clang
    ...
else ifeq ($(OS),Windows_NT)
    CROSS_COMPILE := aarch64-none-elf-
    ...
else
    CROSS_COMPILE := aarch64-linux-gnu-
    ...
endif
```
要点：
- `ifeq` / `else ifeq` / `else` / `endif` 是 Make 自己的条件语法。
- `$(shell ...)` 会在读取 Makefile 时执行一个 shell 命令，把输出作为字符串。
- 最后结果会覆盖同名变量，从而选择不同工具链、不同链接选项。
---
## 7. 把机制串起来：从 `make` 到 `make debug`，到底发生了什么？
结合你现在的 Makefile，我们走一遍：
1. 读入 Makefile：
   - `include config.mk` → 拉进来一些条件源码列表。
   - 设置 `ARCH`、`ROOT_DIR`、`INCLUDES`、`CFLAGS` 等。
   - 根据环境选工具链（Termux/Windows/Ubuntu）。
   - 算出 `SRC_ASM`、`SRC_C`，再映射为 `OBJ`（一堆 `build/xxx.o`）。
   - 增加 Rust 相关变量和规则，定义 `all`、`clean`、`debug` 等。
2. 执行 `make`：
   - 因为 `.DEFAULT_GOAL := all`，终极目标是 `all`。
   - `all` 依赖 `build_dir` 和 `$(TARGET).elf`：
     - `build_dir`：`mkdir -p` 确保目录存在。
     - `$(TARGET).elf`：依赖所有 `.o` 和 Rust 的 `.a`：
       - 每个 `.o` 依赖对应的 `.c/.S`；
       - 只有目标不存在或比源文件旧才重新编译；
       - Rust 的 `.a` 作为普通文件规则，只在该文件不存在或 Rust 源有变时才跑 `cargo build`。
   - 如果任何 `.o` 或 `.a` 更新了，就重新链接 ELF。
3. 执行 `make debug`：
   - 终极目标是 `debug`。
   - `debug` 依赖 `all`，所以先把上面的流程走一遍。
   - 然后执行：
     ```makefile
     debug: all
         $(QEMU) ... -nographic -s -S
     ```
   - QEMU 会以 `-s -S` 启动：等待 GDB 连接，并在入口处暂停。
---
## 8. 常见“坑”速查表（你这次全踩了一遍，建议收藏）
| 现象 | 常见原因 | 如何解决 |
|------|----------|----------|
| `make` 只跑 Rust | 真实文件 `$(RUST_LIB)` 依赖 `.PHONY: rust` | 把 `cargo build` 写在 `$(RUST_LIB):` 下面，去掉 `.PHONY` 依赖 |
| `make` 只跑某一段规则 | 该规则被排成“第一个目标” | 用 `.DEFAULT_GOAL := all` 固定默认目标 |
| 改了 `.c` 不重编 | Make 认为目标已经比前置条件新 | 检查是否误写 `.PHONY`、或者文件时间戳问题（可用 `touch` 调整） |
| `cargo build` 每次都跑 | 同上：真实文件依赖了 `.PHONY` | 同上 |
| 链接时“找不到符号” | `.a` 里没有对应符号 / 链接顺序错误 | 确保 `$(RUST_LIB)` 在 `$(TARGET).elf` 的依赖中；必要时调整链接顺序 |
---
如果你愿意，我可以用你现在的 Makefile 画一整张“依赖图”（从 `all` 到每一个 `.o`、到 Rust 的 `.a`），帮你把整个构建流程再可视化一遍。

