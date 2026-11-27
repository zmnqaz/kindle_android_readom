# 书库封面

`readom-cover.png`（300×405，灰度）是书库里「Readom」这一条的封面：`boot/build_entry.py --icon` 把它写进
`Readom.sh` 开头的 `# Icon: data:image/png;base64,…` 一行，越狱自带的 sh_integration 读到后解出
`documents/Readom.sh.sdr/icon.png`，作为缩略图交给书库（2026-11-27 在第十代 5.18.1.1.1 上验过）。

图是本项目自己画的：一台阅读器，屏幕上是本项目原创的小机器人头（方头、一根天线、带面罩），底边印着 kindle（普通 Roboto 字，不是亚马逊的标志）。`make_cover.py` 生成 1072×1448 原图，再缩到 300 宽；许可同仓库默认。
换图：任何 64 KiB 以内的 PNG 或 JPEG 都行，竖版、灰度在墨水屏上最清楚。
