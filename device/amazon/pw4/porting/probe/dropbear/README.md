# Readom SSH 用的 dropbear

`../ReadomSSH.sh` 在 Kindle 的 Wi-Fi 上开一个 SSH 服务端，好让移植工作从电脑上做，不必每轮插拔、点脚本。
只认密钥、端口 2222，程序在 `/tmp` 运行；防火墙放行和禁止屏保都只到下次重启，不写系统分区。

二进制不进仓库，按下面做：

```sh
# dropbear 2022.83 源码（matt.ucc.asn.au/dropbear/releases/），解开后放入本目录两个文件
CC=arm-linux-gnueabihf-gcc ./configure --host=arm-linux-gnueabihf --disable-zlib \
  --disable-lastlog --disable-utmp --disable-utmpx --disable-wtmp --disable-wtmpx \
  --disable-pututline --disable-pututxline --enable-static
arm-linux-gnueabihf-gcc -O2 -Wall -c pwshim.c -o pwshim.o
make PROGRAMS="dropbear dropbearkey scp" MULTI=1 STATIC=1 LIBS="pwshim.o"
```

- `localoptions.h`：关掉密码登录（没有 `crypt()` 也编不过）、各种转发。
- `pwshim.c`：用户、组、shadow 查询直接读 `/etc/passwd`、`/etc/group`。静态 glibc 的这些函数要走 NSS，
  在 Kindle 上会动态加载它自己的 NSS 模块，每个连接都死在 `_dl_call_libc_early_init: Assertion 'sym != NULL'`
  （登录时的 `getspnam`）。在 Ubuntu 18.04/22.04 容器里用 qemu-arm 都复现不出来，问题只在 Kindle 的 nsswitch 配置上。

电脑这边：放 `../../` 外的私钥，Kindle 上 `readom-ssh/authorized_keys` 放对应公钥；主机密钥第一次运行时生成在
`readom-ssh/host_ed25519`，之后复用。Kindle 的 IP 显示在屏幕左上角，也写在 `readom-ssh/ip.txt`。
