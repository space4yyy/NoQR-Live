#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
bash scripts/install.sh
read -r -p "安装完成。重启 OBS 后添加二维码自动遮挡滤镜。按回车关闭。" _reply
