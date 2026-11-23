# PW4 交接的 QEMU 替身

`boot/handoff/tests/run_qemu_pw4.py` 用 QEMU 的 `mcimx6ul-evk`（Cortex-A7、RAM 在 `0x80000000`）替 PW4：
同一份 4.1.15 源码、同样的 2G/2G 和 `SMP_ON_UP`，所以 lowmem 1:1、模块区在 `0x7f000000` 这些交接依赖的
事实都和 PW4 一样。第一级加载 profile 4 的交接模块，从自己的 `/sys/firmware/fdt` 按设备的做法生成设备树，
被拒绝四种坏输入，跑两次 PROBE（真实的 `pm_test=core` 挂起），然后 COMMIT；第二级是安卓内核的 QEMU 版
（3G/1G、gzip），它的 init 核对命令行和两段 initramfs。

## 2026-11-23 结果

```
PW4_PROBE round=0 pm=0 backend=0 rejected=0 cpu=1 regs=1 gates=1 dcp_stopped=1
PW4_PROBE round=1 pm=0 backend=0 rejected=0 cpu=1 regs=1 gates=1 dcp_stopped=1
PW4_FIRST_KERNEL_READY checks=683
PW4_SECOND_KERNEL uname=4.1.15-lab126-pw4-android81-qemu+ cmdline=console=ttymxc0,115200 rdinit=/init panic=5 pw4_virtual_stage=second
PW4_SECOND_KERNEL_PASS checks=14
```

第二级起来后 lowmem 是 `0xc0000000-0xe0000000`，initrd 在物理 `0x84000000`，就是交接放的位置。

## 怎么搭

1. 镜像：`docker build -t pw4_qemu:22.04 device/amazon/pw4/porting/qemu`（QEMU 6.2、armhf 交叉编译器、cpio）
2. 源码：原厂 4.1.15 树打上 `0001-qemu-pad-imx6ul-clock-mux-parents.patch`（**只给 QEMU 用**：QEMU 的
   CCM 复位值会选到真芯片不会复位到的 mux 下标，越过父时钟数组，`clk_register` 空指针），
   第二级再打上 `../kernel/patches/` 的安卓补丁
3. 第一级配置：原厂 `.config`，`LOCALVERSION="-pw4-loader-smoke"`，`KERNEL_GZIP`，关 `CRYPTO_DEV_FSL_CAAM`
   （QEMU 没有 CAAM，初始化时空指针）和 `IMX_SDMA`（QEMU 的 SDMA 跑不了脚本，挂起后期保存上下文超时）；
   编 `zImage modules imx6ul-14x14-evk.dtb`
4. 第二级配置：`../kernel/android-4.1-draft.config` 合进原厂配置，`LOCALVERSION="-lab126-pw4-android81-qemu"`，关 CAAM
5. 运行：

```sh
python3 boot/handoff/tests/run_qemu_pw4.py --stage1-source <树> --stage1-build <第一级输出> \
  --stage2-source <树> --stage2-build <第二级输出> --tools <gcc-4.9/bin> --qemu-image pw4_qemu:22.04 --output <新目录>
```

## 它证明不了的

PW4 自己的东西 QEMU 都没有：i.MX6SLL 的时钟门控和 DMA 主设备（这里 profile 4 不读 CCM）、PL310 外置 L2
（A7 没有，回调换成空函数）、Falcon BIOS、EPDC。这些要靠设备上的 PROBE（只挂起再恢复、不跳）看实际状态。
