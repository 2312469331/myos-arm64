#!/bin/bash
# 后台新终端运行 make debug
# Windows Git Bash 专用

# 方法1：start 独立 bash（最稳）
start bash -c "make debug; exec bash"

# 方法2：如果上面不行，用这个
# start /MIN bash -c "make debug; exec bash"