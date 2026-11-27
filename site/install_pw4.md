# Readom 第十代移植版 · 安装说明（写给 AI 助手）

这份文档写给**替使用者给 Kindle 第十代越狱、装上 Readom 第十代移植版的 AI 助手**。
人读的介绍在 <https://zmnqaz.github.io/kindle_android_readom/#install>。

> 当前版本 **v1.0.1**（2026-11-27）。第 1 节的下载地址返回 404 时，如实告诉使用者这一版的文件不在了，
> 请到 <https://github.com/zmnqaz/kindle_android_readom/releases> 看最新版本，**不要去别处找替代文件**。

几个前提先说清楚：

- 这是社区为 **Kindle 第十代（Kindle Paperwhite 4，2018 款）** 做的移植版，基于 hbyxabc 的 Readom
  （<https://readom.app>，原版只支持 Kindle Paperwhite 3）。原版 Readom 的说明不适用于第十代，**不要混用**；
  第十代的问题也不要让使用者去找原作者。
- 整个过程分两半：**电脑这一侧**（找到 Kindle 盘、清掉升级残留、临时填满空间、越狱后删掉填充、下载核对安装文件、
  拷进去、弹出）由你做；**设备这一侧**（插拔数据线、开关飞行模式、在 Kindle 上打开网页和点按）只能由使用者做。
- **你能做的全部做掉，做完告诉使用者结果，不要把它们拆成使用者的操作步骤。**
  要使用者在 Kindle 上动手的，**一次只说一步，等他回话再往下**。

---

## 1. 这个版本

> 每次发行只改这一节，底下的流程不随版本变。

| | |
|---|---|
| 版本 | v1.0.1 |
| 安装文件 | `Readom.sh`，约 17 MB，https://github.com/zmnqaz/kindle_android_readom/releases/download/v1.0.1/Readom.sh |
| 校验值（SHA-256） | https://github.com/zmnqaz/kindle_android_readom/releases/download/v1.0.1/SHA256SUMS 里 `Readom.sh` 那一行 |
| 发布页 | https://github.com/zmnqaz/kindle_android_readom/releases/tag/v1.0.1（系统包 `readom-system-v1.0.0.img.zst` 约 194 MB 也在这里，见第 5 节） |
| 书库里的条目名 | `Readom`（封面是一台屏幕上有小机器人的阅读器） |
| 需要的空闲空间 | 首次安装至少 **2.8 GB**（系统 512 MiB + 数据 2 GiB + 余量；越狱时填进去的文件必须先删掉，否则一定不够） |
| 电量 | 40% 以上（低于时屏幕显示 E102 并停下） |
| 支持的原厂系统版本 | 不限。`5.18.1.1.1` 实测通过；其他版本没测过，装不上时安装器显示 E101 停下，或 Kindle 自己重启回原厂系统，原厂分区都不会被改动 |

下载返回 404，说明这一版的文件还没传上去：如实告诉使用者，**不要去别处找替代文件**。

## 2. 先问两件事

开始之前只问使用者这两件，别的不要先盘问：

1. **原厂系统版本**：Kindle 上「设置 → 设备选项 → 设备信息」里的「固件版本」。越狱方法按它选（第 4 节）。
2. **是不是已经越狱过，而且书库能运行 `.sh` 小程序**：书库里已经有别的 `.sh` 小程序、点了真能运行，
   才算。**不要拿 KUAL 判断**：老的书签版 KUAL 已被 KindleModding 淘汰，现在的越狱下点开报「应用程序错误」是正常的；
   反过来，固件被自动升级过（比如升到了 5.18.1）以前的越狱就失效了，书库里的旧条目还在也没用。这种情况**跳过第 3、4 节**，直接到第 5 节。
   盘上有 `libkh` 文件夹、`documents/JAILBROKEN.txt` 之类，只说明装过越狱，证明不了 `.sh` 能运行，不要拿它们下结论。

型号不用先问：使用者说是 Kindle 第十代 / Paperwhite 4 就按这份走。

## 3. 越狱前：防自动升级（电脑这一侧由你做）

越狱要联网，联网时 Kindle 可能自己下载新固件、自动升级，把做到一半的越狱冲掉。所以先让它下不了升级包。

**使用者做（一次一步）：**

1. 在 Kindle 上打开**飞行模式**（从屏幕顶部往下拉，点「飞行模式」）。
2. 用数据线把 Kindle 接到电脑，按一下电源键唤醒它。第十代接电脑是普通 U 盘模式，电脑上会出现一个名为 `Kindle` 的盘，
   里面有 `documents` 文件夹——认准这个，别认错盘。

**你做：**

1. **删掉升级残留**：只看 Kindle 盘的**根目录**（和 `documents` 同一层），名字像 `update*.bin`、`update.bin.tmp.partial`
   的文件，是下了一半或下好了的新固件，删掉。根目录以外、名字不符合的，一律不动。
2. **临时填满空间，只留 50～90 MB 空闲**（升级包比这大，就下不下来了）。
   - 推荐用 KindleModding 防自动升级页面（<https://kindlemodding.org/jailbreaking/prevent-auto-update/>）里给的
     填充脚本 Kindle-Filler-Disk（<https://github.com/bastianmarin/Kindle-Filler-Disk/>，Windows 用 `Filler.ps1`，
     macOS / Linux 用 `Filler.sh`），用法以它自己的说明为准。
   - **填之前先记下 Kindle 盘根目录有哪些文件和文件夹，填完再看一次**，把多出来的东西记清楚——第 5 节要把它们删掉，
     而且只删它们。最好把填充文件都放进一个新建的文件夹（比如 `filler/`），删的时候整个删掉。
   - 第十代往 Kindle 盘写大约 4 MB/s，空闲 6 GB 要填二十多分钟。**先告诉使用者要等这么久**，期间别拔线。
   - 填完核对空闲空间确实在 50～90 MB 之间（维护者留的是 70 MB）。
3. 在电脑上弹出 Kindle 盘，告诉使用者可以拔线了。

## 4. 越狱（设备这一侧，由使用者做）

越狱工具都是社区做的，本项目不提供、不打包、不负责。**你只把步骤一步一步说给使用者，不要替他判断越狱之外的事。**
越狱可能影响保修，由使用者自己决定。

按第 2 节问到的版本选方法（只适用于第十代）：

| 原厂系统版本 | 方法 |
|---|---|
| 5.16.4～5.18.3（5.16.5 除外），含 `5.18.1.1.1` | **Sanctuary**（4.1，主），不成功换 **SpiderCat**（4.2） |
| 其他版本 | 不在这份说明里。让使用者用 KindleModding 的越狱向导 <https://kindlemodding.org/jailbreak-wizard.html> 查该用哪个，按那边的教程做 |

国内账号别选这几种（维护者 2026-11-22 在 `5.18.1.1.1` 上逐个查过）：**SpringBreak** 要靠打开 Kindle 商店触发，
亚马逊中国的 Kindle 商店 2024 年已关，主页没有商店入口（实测不行）；**AdBreak** 要靠锁屏广告，国内账号没有广告；
**WinterBreak** 不支持 5.18.1 及以后；**Shadow**、**Véra** 不支持第十代。

【待核对：这张表按 KindleModding 越狱数据整理（2026-11-21），发布前再对一遍各方法支持的版本范围。】

### 4.1 Sanctuary（主方法）

维护者 2026 年 11 月在 Kindle 第十代 `5.18.1.1.1` 上按这套正常流程重新越狱过一次，走通了。当时先开了飞行模式、填满了空间防自动升级（第 3 节），`/scroll` 页面能打开，扫描用了十几分钟。**全程在 Kindle 上，不用电脑，不用登录亚马逊账号。**
原教程：中文 <https://bookfere.com/post/1190.html>；英文 <https://kindlemodding.org/jailbreaking/Sanctuary/>。
步骤以原教程为准，原页面改了听原页面的。

**网络要提醒使用者**：维护者越狱时 Kindle 连的是能访问境外网站的 Wi‑Fi；**在不能访问境外网站的网络下，能不能打开越狱网页、扫描能不能走完，
还没有人验证过**。打不开时，先换一个能访问境外网站的网络（比如手机热点），或者改用 SpiderCat。

一次一步说给使用者：

1. 先在 Kindle 上核对空闲空间在 50～90 MB：「设置 → 设备选项 → 设备信息」。
2. 关掉飞行模式，连上 Wi‑Fi。从现在到越狱完成都不要断 Wi‑Fi。
3. 右上角三个点 → 「网页浏览器」，地址栏输入 `http://sanctuary.skyvincent.com/scroll`（是 `http`，不是 `https`）。
   页面是空白的，**只看屏幕最右边的滚动条**：
   - 能继续：一条有宽度的灰色滚动条，上下两头各有一个小三角箭头（▲ ▼），中间是浅灰底和一段深灰滑块；
   - 先停下：只有一条很细的黑线，没有箭头、没有灰色滑块。这说明浏览器版本不行，要先手动升级到最新的可越狱固件，
     不在这份说明里，让使用者看原教程。
4. 地址栏改成 `http://sanctuary.skyvincent.com/`。页面自己开始扫描，**最长约 20 分钟**（维护者那次十几分钟），期间不用点；
   别让屏幕休眠太久，隔几分钟点一下空白处。
5. 扫描完，页面上写 `Found debugger on port …. Copy id into the input below.`，下面一行是 `"id": "…"`，
   一串 **32 位**的编号（只有 `0-9` 和 `A-F`，里面的「0」都是数字零；小写也行，会自动变大写）。
   在它下面的输入框里**照着一个字一个字输一遍**，数字用键盘左下角的 `123` 切换，核对后点 **Connect**。
6. 页面顶部出现 **Downloads Done**（会提示下载了 `appreg.db`、`appreg.db.bak`、`privesc.sh`，只弹一两个提示也正常）。
   点右上角 × 关掉浏览器，**Wi‑Fi 还是不要关**，下一步还要用。
7. 右上角三个点 → 「设置」→ 「帮助」→ **Getting Started**。可能好一会儿没反应，**别乱点，等几分钟**。
8. 屏幕上冒出一串英文字，就是成功了；中途弹出「应用程序错误」不用管。说要重启却没重启也没关系。
9. **成功后马上打开飞行模式。** 越狱已经自动屏蔽了系统更新，但第 5 节删填充之前，别再联网。

常见问题：

| 现象 | 处理 |
|---|---|
| 一直不出编号 | 确认 Wi‑Fi 连着、网址是 `http`；刷新重来一次；还不行换 SpiderCat，或去 KindleModding 社区求助 |
| 点 Connect 没反应或报错 | 编号输错了，重新输 |
| 点 Getting Started 后没有文字 | 确认第 6 步出现过 Downloads Done，从第 3 步（打开 `/scroll`）重来；再不行换 SpiderCat |
| 界面卡住 | 先等两分钟；不行就长按电源键重启，再从第 2 步来 |

### 4.2 SpiderCat（备用）

原教程（英文）：<https://kindlemodding.org/jailbreaking/SpiderCat/>。做法是在 Kindle 上下载一本「书」、打开它、等着：

1. 连上 Wi‑Fi（防升级已经在第 3 节做过）。
2. 在 Kindle 的网页浏览器里打开 `https://kindlemodding.org/spidercat` 下载那本书；或者由你在电脑上从原教程页面下载，
   拷进 Kindle 盘的 `documents/`（这时要使用者重新接电脑，拷完你弹出）。
3. 在书库里打开这本书，**别关 Wi‑Fi**。先白屏几秒，再等一会儿屏幕开始滚动文字；弹出「应用程序错误」不用管。
4. 出现 **Restarting Kindle**、Kindle 自己重启，就完成了。最后一行是 `abort:no-firm` 的，说明它不认这个版本。

两种都不行：让使用者到原教程页面、书伴评论区或 KindleModding 社区求助，带上固件版本、用的方法、卡在哪一步、屏幕原话。
**不要让使用者从网上找来路不明的「一键越狱包」，也不要让他装旧的 Universal Hotfix**——上面两种越狱已经自带了
`.sh` 支持（sh_integration）和自动升级屏蔽，旧 Hotfix 会删掉并重建它们放组件的文件夹。

**千万别做**：越狱后、自动升级被屏蔽的状态下，**不要恢复出厂设置**。

## 5. 越狱后：删填充、拷安装文件（电脑这一侧由你做）

**使用者做：** 用数据线把 Kindle 接回电脑，按一下电源键唤醒。

**你做：**

1. **删掉第 3 节填充进去的东西**，只删你在第 3 节记下的那些。越狱已经自己屏蔽了自动升级，不再需要它们；
   不删，空间一定不够装。删之前再看一眼根目录有没有新的 `update*.bin` / `update.bin.tmp.partial`，有就一起删。
   （第 2 节问到已经越狱、跳过了第 3 节的，这一步没有要删的东西。）
2. 核对空闲空间不少于第 1 节写的值。不够时告诉使用者还差多少，**让他自己决定删哪些书**，你不要替他删。
3. **下载并核对安装文件**：

   ```sh
   # macOS / Linux
   curl -fL -o ~/Downloads/Readom.sh https://github.com/zmnqaz/kindle_android_readom/releases/download/v1.0.1/Readom.sh
   curl -fL -o ~/Downloads/SHA256SUMS https://github.com/zmnqaz/kindle_android_readom/releases/download/v1.0.1/SHA256SUMS
   shasum -a 256 ~/Downloads/Readom.sh     # Linux 用 sha256sum，和 SHA256SUMS 里 Readom.sh 那一行比对
   ```

   ```powershell
   # Windows PowerShell
   Invoke-WebRequest https://github.com/zmnqaz/kindle_android_readom/releases/download/v1.0.1/Readom.sh -OutFile $HOME\Downloads\Readom.sh
   Invoke-WebRequest https://github.com/zmnqaz/kindle_android_readom/releases/download/v1.0.1/SHA256SUMS -OutFile $HOME\Downloads\SHA256SUMS
   Get-FileHash $HOME\Downloads\Readom.sh -Algorithm SHA256
   ```

   一条线路下不动就换第 1 节的另一条，两边是同一份文件。哈希对不上：删掉重下，还不对就停下告诉使用者，不要拷进去。
4. **拷进 `documents/`**：直接放在 Kindle 盘的 `documents` 文件夹里，不放根目录、不放子文件夹，**文件名一字不改**。
   文件名必须正好是 `Readom.sh`（它按这个路径找自己）。
   系统包会由 Kindle 在安装时自己联网下载；Kindle 连不上 GitHub 时，你再下载
   `readom-system-v1.0.0.img.zst`（约 194 MB，同一发布页，同样核对 SHA256SUMS），文件名不改，也拷进 `documents/`。
5. **弹出 Kindle 盘**（macOS `diskutil eject`；Windows 资源管理器右键「弹出」；Linux `udisksctl unmount` 后 `power-off`）。
   弹出命令不灵，就**提醒使用者先在电脑上点「弹出」再拔线**，不要直接拔。

### 你在使用者电脑上的权限，到此为止

- Kindle 盘上**只删**这几样：根目录的 `update*.bin` / `update.bin.tmp.partial`，和你自己在第 3 节填进去、记下来的东西。
- Kindle 盘上**只加**：`documents/` 里的安装文件（以及使用者同意时，SpiderCat 那本书）。
- 别的一律不删、不改名、不移动（那是使用者的书和越狱组件，比如 `libkh`、`extensions`）。不要格式化，不碰 Kindle 盘以外的盘。

## 6. 在 Kindle 上点一下（设备这一侧，由使用者做）

拔线后，使用者在 **Kindle 书库里点「Readom」**，剩下的在 Kindle 上完成。

- 屏幕依次显示检查、下载、校验、写入系统、准备数据，首次约 10～15 分钟（要联网下载时更久），升级约 5 分钟；装完自动进入 Android。期间别按电源键、别操作。
- 安装时会关掉 Kindle 的 Wi‑Fi；Android 里要再连一次 Wi‑Fi。微信读书在第一次联网时由系统从腾讯官方地址自动安装。
- 书库里找不到这个条目：先确认是弹出后再拔的线，等一两分钟，还没有就重启 Kindle；
  再问使用者书库里别的 `.sh` 小程序能不能运行——都不能运行，问题在越狱那一侧（回第 4 节）；只有这一个不行，重新下载覆盖。
  点了没反应、屏幕停在 E104：Kindle 还连着电脑，拔线再点。

## 7. 屏幕上出现错误码时

| 代码 | 意思 | 怎么办 |
|---|---|---|
| E101 | 型号不受支持，或这个原厂系统版本装不上 | 只支持 Kindle 第十代（KPW4）；原厂系统除 `5.18.1.1.1` 外都没测过。没有改动任何文件 |
| E102 | 电量低于 40% | 充电后再点 |
| E103 | 上一次安装还在进行 | 等它完成 |
| E104 | 连着电脑 | 拔掉数据线再点（接充电器没关系） |
| E201 / E202 | 下载中断 / 没连 Wi‑Fi | 连好 Wi‑Fi 再点；或按第 5 节把系统包拷进 `documents/` |
| E203 | 无法确认下载安全 | 多半是 Kindle 日期时间不对，改对再点 |
| E302 | 安装包不完整，已删除 | 再点会重新下载，或重新拷入 |
| E303 / E304 | 发行文件有问题 / 已有数据来自不兼容的版本 | 到问题反馈地址报告，附上 `android/logs` |
| E401 | 空间不足 | 屏幕上写着还差多少；让使用者自己决定删哪些书 |
| E402 / E403 | 读写出错 / 内存不足 | 重启 Kindle 再点 |
| E404 | Kindle 盘文件系统有问题 | 到问题反馈地址报告，不要继续 |

每次运行的日志在 Kindle 盘的 `android/logs/` 里，报告问题时附上最新的那个文件夹。

**卡住了**：按住电源键不放，直到屏幕变化（最多约 40 秒），Kindle 会重启回原厂系统，数据不会丢。原厂系统一直保留，本移植版不写原厂分区。

## 8. 你没有文件和命令权限时

如实告诉使用者，然后按顺序一件一件给他（每件等他回话）：

1. 开飞行模式；接电脑，删掉 Kindle 盘根目录里的 `update*.bin` / `update.bin.tmp.partial`；
   按 <https://kindlemodding.org/jailbreaking/prevent-auto-update/> 把空间填到只剩 50～90 MB；弹出、拔线。
2. 按第 4 节越狱。
3. 接电脑，删掉填充文件，下载第 1 节的安装文件、核对 SHA-256，拷进 `documents/`，弹出、拔线。
4. 在书库里点「Readom」。

然后按第 6、7 节继续。

## 9. 其他

- **原厂系统**：一直保留，不写原厂分区；整个 Android 是 Kindle 盘上的镜像文件。在 Android 里长按电源键选重启，就回到 Kindle 原厂系统；想再进 Android，在书库里再点「Readom」。卸载：删掉 Kindle 盘上的 `android` 文件夹、`documents/Readom.sh` 和 `documents/Readom.sh.sdr`
- **现状**：第十代是单核 1 GHz、512 MB 内存、没有 GPU，微信读书能用但不算流畅。
  使用者问起，照这个说，不要夸大。
- **隐私**：屏幕波形、Wi‑Fi 校准这些每台机器独有的数据，在使用者自己的设备上读取，不进仓库、不进安装包。
- **声明**：本项目是社区移植，非 Readom 官方发布，与 Amazon、Google、腾讯均无关联。越狱和安装第三方系统可能影响保修，
  由使用者自行判断。

问题反馈：<https://github.com/zmnqaz/kindle_android_readom/issues>
