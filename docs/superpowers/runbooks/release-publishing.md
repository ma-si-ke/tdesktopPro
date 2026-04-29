# 发包流程 Runbook

> **适用范围**：把 Telegram Desktop fork（`customization` 分支）的新版本通过自定义更新通道发布给员工。
>
> **第一次发包提交**：`8b6c878` (`custom-update-server-v1`)、`0e9af0d`（packer 公钥替换）、`6fea6aa`（首次 6.7.7 bump）。

---

## 0. 系统组成（速览）

```
版本号 → 重编 Release Telegram.exe + Updater.exe
   ↓ Packer 用 RSA 私钥签名 + LZMA 压缩
tx64upd<version>（二进制更新包）
   ↓ gh release create / upload --clobber
GitHub Release asset
   ↓ 改 current4 manifest 指向 release URL
raw.githubusercontent.com/.../current4
   ↓ 客户端 HttpChecker 拉 manifest → 下载包 → 用 config.h 公钥验签 → LZMA 解压 → 替换自己 → 重启
新版客户端
```

**密钥**：
- 私钥（签名）：`<repo-parent>/DesktopPrivate/packer_private.h`（PKCS#1 RSA-1024）
- 公钥（验签）：`Telegram/SourceFiles/config.h` 的 `UpdatesPublicKey` / `UpdatesPublicBetaKey`，必须与 `Telegram/SourceFiles/_other/packer.cpp` 顶部的 `PublicKey` / `PublicBetaKey` 字面值完全一致

**仓库**：
- 客户端：本仓库（`tdesktop` 目录、`customization` 分支）
- 更新服务端：`https://github.com/kakala666/TelegramProClientUpdate`（manifest + release assets）

---

## 1. 一次性配置（第一次部署时做过，仅留作参考）

> 如果你接手一台从零开始的开发机，按这一节执行；否则跳到 §2。

### 1.1 替换 packer.cpp 内的上游公钥
`Telegram/SourceFiles/_other/packer.cpp` 第 14-28 行的 `PublicKey` / `PublicBetaKey` 必须与 `config.h` 中对应的公钥**逐字节一致**。这两个公钥用于 Packer 签名后的"自检"步骤。

### 1.2 创建私钥头文件
路径：`<repo-parent>/DesktopPrivate/packer_private.h`（**仓库外**，永远不进 git）。

格式（每行 PEM 内容末尾加 `\n\` 续行符）：
```cpp
#pragma once

const char *PrivateKey = "\
-----BEGIN RSA PRIVATE KEY-----\n\
<base64 内容多行>\n\
-----END RSA PRIVATE KEY-----\
";

const char *PrivateBetaKey = "\
-----BEGIN RSA PRIVATE KEY-----\n\
<base64 内容多行>\n\
-----END RSA PRIVATE KEY-----\
";
```

**格式要求**：
- `PEM_read_bio_RSAPrivateKey()` 只认 PKCS#1（`BEGIN RSA PRIVATE KEY`）。如果你手上的私钥是 PKCS#8（`BEGIN PRIVATE KEY`），用 `openssl rsa -in stable_private.pem -traditional` 转一次

### 1.3 创建 alpha_private.h 占位
路径：`<repo-parent>/DesktopPrivate/alpha_private.h`。我们不发 closed-alpha，所以填空字符串即可：
```cpp
#pragma once
const char *AlphaPrivateKey = "";
```
但这个文件**必须存在**，否则 packer.cpp 第 33 行无条件 include 会编译失败。

### 1.4 让 CMake 把 Packer target 加进解决方案
默认配置不会编 Packer。在你那个 x64 Native Tools Command Prompt 里：
```cmd
cd /d D:\ProjectKaka\TelegramProClient\tdesktop
cmake -S . -B out -DDESKTOP_APP_SPECIAL_TARGET=win64
```
（`win64` 这个 SPECIAL_TARGET 在 64 位 Windows 上对 Telegram 主程序没有副作用，只是把 Packer 加进解决方案。）

### 1.5 编 Packer
```cmd
cmake --build out --config Release --target Packer
```
产出：`out\Release\Packer.exe`（约 22 MB）。这个 exe 一次编完长期复用，除非 packer.cpp 或两个私钥头文件改了才需要重编。

---

## 2. 每次发新版的步骤

### 2.1 改版本号（两个文件，必须同步）

**a) `Telegram/build/version`** —— 把 6 个含版本的字段全改：
```
AppVersion         6007007
AppVersionStrMajor 6.7
AppVersionStrSmall 6.7.7
AppVersionStr      6.7.7
BetaChannel        0
AlphaVersion       0
AppVersionOriginal 6.7.7
```

**b) `Telegram/SourceFiles/core/version.h:25-26`** —— 改两行：
```cpp
constexpr auto AppVersion = 6007007;
constexpr auto AppVersionStr = "6.7.7";
```

> ⚠️ 这俩**必须同步**。tdesktop 的 build 系统**没有**从 `build/version` 自动生成 `version.h` 的步骤；前者是给打包脚本看的、后者是给 C++ 代码看的。
>
> ⚠️ 版本号编码规则：`AppVersion = major * 1000000 + minor * 1000 + patch`。例如 6.7.7 → `6007007`，6.8.0 → `6008000`。

### 2.2 提交版本号 bump

```cmd
git add Telegram/build/version Telegram/SourceFiles/core/version.h
git commit -m "chore: bump version to X.Y.Z"
```

### 2.3 重编 Release Telegram

```cmd
cmake --build out --config Release --target Telegram
```

约 1-3 分钟（增量编译）。完成后产出：
- `out\Release\Telegram.exe`（约 213 MB）
- `out\Release\Updater.exe`（约 176 KB）

> **Release vs Debug**：发包**必须**用 Release。Debug 体积巨大、性能差，且默认不带自动更新代码。
>
> **PCH 报错可忽略**：偶尔会看到 `LNK4099` 大量警告 + 一两条 `xxx.pdb 不是创建此预编译头时使用的 pdb 文件` 错误。这通常是次要 target 上的并发问题，**不影响 `Telegram.exe` 链接成功**。验证方法：
> ```bash
> # 在 WSL 里
> printf "Version 6.7.X count: "
> LC_ALL=C grep -aoc "X\.Y\.Z" out/Release/Telegram.exe
> ```
> 应该恰好出现 1 次新版本字符串、0 次旧版本字符串。

### 2.4 准备打包目录

把 `Telegram.exe` 和 `Updater.exe` 复制到一个**只放这两个文件**的干净目录：
```cmd
rmdir /s /q out\Release\a 2>nul
mkdir out\Release\a
copy out\Release\Telegram.exe out\Release\a\
copy out\Release\Updater.exe out\Release\a\
dir out\Release\a
```

`dir` 输出应该恰好显示 2 个文件。

### 2.5 跑 Packer 签名打包

> ⚠️ **关键陷阱**：`-path` 参数后面**必须跟具体文件路径**，不能跟目录。如果传目录，目录名会被烙进包内每个文件的相对路径，导致 Updater 把新版释放到 `<install>/<dirname>/` 而不是 `<install>/`，更新表面成功实际无效。

```cmd
del tx64upd6007007 2>nul
out\Release\Packer.exe -path out\Release\a\Telegram.exe -path out\Release\a\Updater.exe -target win64 -version 6007007
```

**预期输出**（关键几行，按顺序）：
```
Found 2 files..
Telegram.exe (213145088)        ← 没有目录前缀
Updater.exe (176640)             ← 没有目录前缀
Compression start, size: ...
Compressed to size: ...
Counting SHA1 hash..
Signing..
Checking signature..
Signature verified!              ← 必须看到这行
Update file 'tx64upd6007007' written successfully!
```

**验收标准**：
1. `Telegram.exe` 和 `Updater.exe` 这两行**前面没有 `a/` 或任何前缀**
2. 看到 `Signature verified!`（说明私钥与 packer.cpp 公钥配对正确）

如果看到 `Signature verification failed!`，说明 1.1 / 1.2 的密钥配对不对，停下来排查 —— 客户端那边的 config.h 公钥与 packer.cpp / packer_private.h 三方必须同源。

产物：当前工作目录下生成 `tx64upd<version>` 文件（**没有扩展名**），约 30-50 MB。

### 2.6 创建 / 更新 GitHub Release

```bash
# WSL 终端，进入 tdesktop 仓库
gh release create vX.Y.Z tx64updNNNNNN \
    -R kakala666/TelegramProClientUpdate \
    -t "X.Y.Z" \
    -n "<release notes>"
```

如果 release 已存在（你需要替换 asset 重发）：
```bash
gh release upload vX.Y.Z tx64updNNNNNN \
    -R kakala666/TelegramProClientUpdate \
    --clobber
```

`--clobber` 会覆盖同名文件，URL 不变（`https://github.com/.../releases/download/vX.Y.Z/tx64updNNNNNN`）。

### 2.7 改 manifest

```bash
# 取得当前 current4 的 sha
CURRENT_SHA=$(gh api 'repos/kakala666/TelegramProClientUpdate/contents/current4' --jq '.sha')

# 准备新内容
NEW_CONTENT='{
  "win64": {
    "stable": {
      "released": <int 版本号，如 6007007>,
      "link": "https://github.com/kakala666/TelegramProClientUpdate/releases/download/v<X.Y.Z>/tx64upd<NNNNNN>"
    }
  }
}
'
NEW_B64=$(printf '%s' "$NEW_CONTENT" | base64 -w0)

# PUT
gh api -X PUT 'repos/kakala666/TelegramProClientUpdate/contents/current4' \
    -f message="Release X.Y.Z" \
    -f content="$NEW_B64" \
    -f sha="$CURRENT_SHA"
```

校验 raw 端：
```bash
sleep 3 && curl -sf 'https://raw.githubusercontent.com/kakala666/TelegramProClientUpdate/refs/heads/main/current4'
```
应返回新 JSON。

### 2.8 端到端验证

找一台还在跑**老版本**的客户端：
1. Settings → Advanced → Check for updates
2. 应该看到：`Checking...` → `Downloading update...`（进度条 ~30-50 MB）→ `Update ready` + `Restart Telegram` 按钮
3. 点 Restart → Updater 介入 → Telegram 自动重启
4. Settings → Advanced 顶部应该显示新版本号

### 2.9 打 git tag

更新成功后：
```bash
git tag -a release-vX.Y.Z -m "Released X.Y.Z to employees"
# 推 customization 分支（含版本 commit）和 tag
git push origin customization
git push origin release-vX.Y.Z
```

---

## 3. 常见坑（实战经验）

### 3.1 `-path` 传目录而非文件
**症状**：Packer 输出 `a/Telegram.exe`、`a/Updater.exe` 这种带前缀的文件名。客户端更新触发、下载完成、重启后**仍是旧版**，安装目录里多了一个 `a` 子文件夹。

**原因**：Packer 把 `-path` 参数的父目录作为相对路径起点。如果传的是目录 `out\Release\a`，父目录是 `out\Release`，包内每个文件的相对路径就是 `a/Telegram.exe`，被解压到 `<install>/a/...` 而不是 `<install>/`。

**修复**：见 §2.5，必须传**每个文件**作为独立 `-path`。

### 3.2 重编 Release 之前直接打包
**症状**：客户端"更新成功"重启后，看版本还是旧版本，又触发更新 → 死循环。

**原因**：把 vX 版本的 `Telegram.exe` 当作 vY 来打包。客户端下载 vY 包，但解压出的二进制内置 `AppVersion` 还是 vX。manifest 里 `released: vY > vX = current`，无限触发。

**修复**：发包前**必须先 §2.1 改版本号 + §2.3 重编**，确认二进制里 `AppVersion` 字符串已经是新版本（用 `LC_ALL=C grep -aoc` 验）。

### 3.3 客户端 tdata 里有 `prefix` 文件残留
**症状**：HttpChecker 请求 URL 不是我们的 GitHub raw，而是 `https://td.telegram.org`。下载到 47.5 MB 的官方包，签名验证失败。

**原因**：`mtp_instance.cpp:935-937` 在历史版本里会把 Telegram 服务器 config 推下来的 `autoupdate_url_prefix` 写入 `tdata/prefix`。我们已经在 commit `8b6c878` 移除这段代码，但**升级前装的客户端**可能仍有残留 `tdata/prefix` 文件。

**修复**：升级到含 `8b6c878` 的二进制后，残留 prefix 不会再被覆盖；但首次升级前，可手动 `rm <workdir>/tdata/prefix`。

### 3.4 版本号 `version.h` 与 `build/version` 不同步
**症状**：编译时不报错，但发布后客户端识别不到新版本（或报版本号怪异）。

**原因**：`version.h` 是 C++ 代码用的、`build/version` 是给打包脚本看的。我们的 build 系统**没有**从 `build/version` 自动生成 `version.h`。

**修复**：每次必须手改两个文件，必须同步。

### 3.5 测试客户端清理
触发过失败更新后，测试客户端可能处于脏状态：
- `<install>` 下出现 `a/` 子目录（如果中过 §3.1 的坑）
- `<workdir>/tdata/tupdates/` 缓存有上次下载的包

**清理**（关闭客户端后）：
```cmd
:: 装机版 workdir：%APPDATA%\Telegram Desktop\
:: Debug 测试 workdir：D:\ProjectKaka\TelegramProClient\tdesktop\out\Debug\
rmdir /s /q "<install>\a"
rmdir /s /q "<workdir>\tdata\tupdates"
```

---

## 4. 快速参考：每次发包要跑的命令

假设要发 6.7.8（即 6007008）：

```bash
# 1. 改版本号（手工编辑两个文件）
#    - Telegram/build/version
#    - Telegram/SourceFiles/core/version.h:25-26

# 2. 提交
git add Telegram/build/version Telegram/SourceFiles/core/version.h
git commit -m "chore: bump version to 6.7.8"
```

```cmd
:: 3. 重编（Windows cmd, x64 Native Tools Prompt）
cd /d D:\ProjectKaka\TelegramProClient\tdesktop
cmake --build out --config Release --target Telegram

:: 4. 准备打包目录
rmdir /s /q out\Release\a 2>nul
mkdir out\Release\a
copy out\Release\Telegram.exe out\Release\a\
copy out\Release\Updater.exe out\Release\a\

:: 5. 签名打包
del tx64upd6007008 2>nul
out\Release\Packer.exe -path out\Release\a\Telegram.exe -path out\Release\a\Updater.exe -target win64 -version 6007008
```

```bash
# 6. 上传 + 改 manifest（WSL）
cd /mnt/d/ProjectKaka/TelegramProClient/tdesktop

gh release create v6.7.8 tx64upd6007008 \
    -R kakala666/TelegramProClientUpdate \
    -t "6.7.8" \
    -n "Release notes here"

CURRENT_SHA=$(gh api 'repos/kakala666/TelegramProClientUpdate/contents/current4' --jq '.sha')
NEW_CONTENT='{
  "win64": {
    "stable": {
      "released": 6007008,
      "link": "https://github.com/kakala666/TelegramProClientUpdate/releases/download/v6.7.8/tx64upd6007008"
    }
  }
}
'
NEW_B64=$(printf '%s' "$NEW_CONTENT" | base64 -w0)
gh api -X PUT 'repos/kakala666/TelegramProClientUpdate/contents/current4' \
    -f message="Release 6.7.8" \
    -f content="$NEW_B64" \
    -f sha="$CURRENT_SHA"

# 7. tag 客户端 commit
git tag -a release-v6.7.8 -m "Released 6.7.8 to employees"
git push origin customization
git push origin release-v6.7.8
```

---

## 5. 回滚

如果新版本上线后发现问题：

**方法 A（推荐）：回退 manifest 让员工继续用上一版**
```bash
CURRENT_SHA=$(gh api 'repos/kakala666/TelegramProClientUpdate/contents/current4' --jq '.sha')
ROLLBACK_CONTENT='{
  "win64": {
    "stable": {
      "released": <上一个稳定版的 int>,
      "link": "https://github.com/kakala666/TelegramProClientUpdate/releases/download/v<上一个稳定版>/tx64upd<...>"
    }
  }
}
'
ROLLBACK_B64=$(printf '%s' "$ROLLBACK_CONTENT" | base64 -w0)
gh api -X PUT 'repos/kakala666/TelegramProClientUpdate/contents/current4' \
    -f message="Rollback to <上一个稳定版>" \
    -f content="$ROLLBACK_B64" \
    -f sha="$CURRENT_SHA"
```

> ⚠️ 注意：客户端 `validateLatestUrl` 检查 `availableVersion <= myVersion` 时会返回空（不更新）。所以**已经升上去的员工不会回退**，只是后面没升的员工不会再升。
> 如果需要强制已升级员工降级，得发"反向新版"（版本号更高、但内容是旧版二进制），这操作上等于发新版，要谨慎。

**方法 B：删 release**
```bash
gh release delete vX.Y.Z -R kakala666/TelegramProClientUpdate -y
```
manifest 指向的 URL 会 404，已升级员工不受影响，未升级员工的 Check for updates 会报失败 —— 这通常**不是**你想要的，A 比 B 好。

---

## 6. 文件清单速查

| 路径 | 内容 | 是否进 git |
|---|---|---|
| `Telegram/build/version` | 版本号字符串、build 用 | ✅ |
| `Telegram/SourceFiles/core/version.h` | 版本号 C++ 常量 | ✅ |
| `Telegram/SourceFiles/_other/packer.cpp` | Packer 工具源码 + 公钥字面值 | ✅ |
| `Telegram/SourceFiles/config.h` | 客户端验签公钥 | ✅ |
| `<repo-parent>/DesktopPrivate/packer_private.h` | 私钥（PKCS#1 PEM） | ❌ **永远不能进 git** |
| `<repo-parent>/DesktopPrivate/alpha_private.h` | alpha 私钥占位（空） | ❌ |
| `<repo-parent>/telgram发包密钥/*.pem` | 私钥/公钥 PEM 原件备份 | ❌ |
| `out/Release/Packer.exe` | 编出来的 packer 工具 | ❌（build 产物） |
| `tx64upd<version>` | 签名后的更新包 | ❌（build 产物，发布到 GitHub Release） |
| `kakala666/TelegramProClientUpdate@main:current4` | 服务端 manifest | 在另一个 repo |
| `kakala666/TelegramProClientUpdate releases vX.Y.Z` | release asset | 在另一个 repo |
