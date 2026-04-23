#!/bin/bash
# 后台新终端运行 make debug
# Windows Git Bash 专用

# 完美：不闪退 + 中文不乱码
start bash -c "chcp.com 65001; make debug; exec bash"