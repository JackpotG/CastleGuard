#!/bin/zsh
set -eu
cd -- "${0:A:h}"
if ! python3 tools/planner-app/build.py; then
    print '应用生成失败，请保留上面的错误信息。按回车关闭。'
    read -r
    exit 1
fi
open -a "$PWD/游戏策划配置.app"
