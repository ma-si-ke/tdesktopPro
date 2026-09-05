# 自建 Windows 编译机接入 GitHub Actions —— 完整教程

把一台全新的 **Windows 11 专业版**服务器,配成本仓库 `win.yml` 的 **self-hosted runner**,替换原来的 Depot 机器。全程在服务器上用 RDP、以**管理员**操作。

> 关键版本(取自 `docs/building-win.md`,必须精确匹配):
> - Visual Studio **2026**(安装目录 `...\Microsoft Visual Studio\18\`)
> - MSVC 工具集 **14.44**(`v144.4`,`-vcvars_ver=14.44`)
> - Windows SDK **10.0.26100.0**
> - Python **3.10**

---

## 阶段 0 · 规划与前提

- 硬件建议:≥8 核、≥16GB 内存、**≥120GB 空闲磁盘**(依赖库 + Qt/WebRTC 源码很大)。
- 你需要:本仓库的**管理员权限**(拿 runner 注册 token)。
- 仓库 Secrets 里已应有:`TG_API_ID`、`TG_API_HASH`、`DESKTOP_PRIVATE_PACKER_B64`、`DESKTOP_PRIVATE_ALPHA_B64`(self-hosted 照样能读,不用动)。
- **安全前提**:仓库必须是**私有**的。self-hosted runner 接公开仓库 = 任何人发 PR 就能在你服务器上跑任意代码。

---

## 阶段 1 · 安装第三方软件

### 1.1 启用开发者模式(`mklink` 需要)
workflow 第一步用 `mklink /d` 建符号链接,需要该权限。管理员 PowerShell:

```powershell
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock" /t REG_DWORD /f /v "AllowDevelopmentWithoutDevLicense" /d "1"
```
或:设置 → 系统 → 开发者选项 → 打开"开发人员模式"。

### 1.2 Git for Windows
- 下载 https://git-scm.com/download/win,默认选项安装(自带 **Git Bash**,提供 workflow 里 bash 步骤要用的 `sha256sum` / `sed` / `base64` / `find`)。
- 验证:`git --version`。

### 1.3 Python 3.10
- 下载 3.10.x 安装器 https://www.python.org/downloads/(**务必勾选 Add python.exe to PATH**)。
- 验证:`python --version` 输出 `3.10.x`。

### 1.4 CMake(装到默认的 `C:\Program Files\CMake`)
- 官方 MSI https://cmake.org/download/,安装时选 **Add CMake to the system PATH for all users**,保持默认安装目录。
- 验证:`cmake --version`;并确认目录 `C:\Program Files\CMake\share\` 存在。
- 为什么单独装:workflow 有一步 `sed` 补丁改的正是 `C:\Program Files\CMake\...\Windows-MSVC.cmake`,路径不对这步会失败。

### 1.5 NuGet CLI
- 下载 https://dist.nuget.org/win-x86-commandline/latest/nuget.exe,放到一个目录(如 `C:\Tools\`),把该目录加入**系统 PATH**。
- 验证:`nuget help` 有输出。

### 1.6 Visual Studio 2026 Build Tools(最关键、最容易出错)
- 下载 https://aka.ms/vs/18/stable/vs_buildtools.exe。
- 用安装器 GUI,勾选:
  - **工作负载**:使用 C++ 的桌面开发(Desktop development with C++)
  - **单个组件**里额外确认勾上(搜索关键字):
    - `MSVC v144.4 (14.44)` x86/x64 生成工具 —— 搜 **14.44**
    - `Windows 11 SDK (10.0.26100.0)` —— 搜 **26100**
    - `用于最新 v144 生成工具的 C++ ATL`(x86/x64)
- 命令行等价(组件 ID 以安装器为准,建议装完用 GUI 复核 14.44 是否勾上):

```powershell
.\vs_buildtools.exe --quiet --wait --norestart --nocache `
  --add Microsoft.VisualStudio.Workload.VCTools `
  --add Microsoft.VisualStudio.Component.Windows11SDK.26100 `
  --add Microsoft.VisualStudio.Component.VC.ATL `
  --includeRecommended
```
> 命令行不一定带上 14.44 这个非默认工具集,**务必再开 GUI 搜 "14.44" 确认已安装**。
- 验证:存在 `C:\Program Files\Microsoft Visual Studio\18\<版本>\VC\Auxiliary\Build\vcvars64.bat`。

---

## 阶段 2 · 环境自检脚本

把下面存成 `check-build-env.ps1`,**管理员 PowerShell** 运行,逐项确认全绿再往下走。

```powershell
$ok = $true
function Check($name, $cond, $hint) {
  if ($cond) { Write-Host "[ OK ] $name" -ForegroundColor Green }
  else { Write-Host "[FAIL] $name  ->  $hint" -ForegroundColor Red; $script:ok = $false }
}

Check "Git" ($null -ne (Get-Command git -ErrorAction SilentlyContinue)) "安装 Git for Windows"

$bash = "$env:ProgramFiles\Git\bin\bash.exe"
Check "Git Bash 工具(sha256sum/sed/base64)" `
  ((Test-Path $bash) -and (& $bash -lc "command -v sha256sum && command -v sed && command -v base64" 2>$null)) `
  "确保装的是 Git for Windows(自带 Git Bash)"

$pyver = (python --version 2>&1)
Check "Python 3.10" (($null -ne (Get-Command python -ErrorAction SilentlyContinue)) -and ($pyver -match "3\.10")) "装 Python 3.10 并 Add to PATH（当前: $pyver）"

Check "CMake 在 PATH" ($null -ne (Get-Command cmake -ErrorAction SilentlyContinue)) "装 CMake 并加入 PATH"
Check "CMake 在 Program Files\CMake" (Test-Path "$env:ProgramFiles\CMake\share") "用官方 MSI 装到默认目录（sed 补丁依赖此路径）"

Check "NuGet CLI" ($null -ne (Get-Command nuget -ErrorAction SilentlyContinue)) "把 nuget.exe 放进 PATH"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = if (Test-Path $vswhere) { & $vswhere -products * -latest -property installationPath } else { $null }
Check "Visual Studio + C++" (($vs) -and (Test-Path "$vs\VC\Auxiliary\Build\vcvars64.bat")) "装 VS 2026 Build Tools + 使用 C++ 的桌面开发"
Check "MSVC 工具集 14.44" (($vs) -and (Get-ChildItem "$vs\VC\Tools\MSVC" -ErrorAction SilentlyContinue | Where-Object { $_.Name -like "14.44*" })) "在 VS 安装器里勾选 MSVC v144.4 (14.44)"
Check "Windows SDK 10.0.26100.0" (Test-Path "${env:ProgramFiles(x86)}\Windows Kits\10\Include\10.0.26100.0") "在 VS 安装器里勾选 Windows 11 SDK 10.0.26100"

$dev = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock" -Name AllowDevelopmentWithoutDevLicense -ErrorAction SilentlyContinue).AllowDevelopmentWithoutDevLicense
Check "开发者模式(mklink)" ($dev -eq 1) "启用开发人员模式"

Write-Host ""
if ($ok) { Write-Host "全部通过,可以继续。" -ForegroundColor Green }
else { Write-Host "有未通过项,先补齐再继续。" -ForegroundColor Yellow }
```

---

## 阶段 3 · (强烈建议)先手动冷编一次

在接 CI 之前,先在服务器手动完整编一遍,确认环境没问题——否则一轮 CI 失败要等很久。

1. 开一个初始化了 VS 环境的终端(照 `docs/building-win.md`):
   ```
   %comspec% /k "C:\Program Files\Microsoft Visual Studio\18\<版本>\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
   ```
2. 准备目录并拉代码(用你自己的私有仓库地址):
   ```
   mkdir D:\TBuild
   cd /d D:\TBuild
   git clone --recursive <你的仓库地址> tdesktopPro
   ```
3. 编依赖(这一步 ~2 小时,冷):
   ```
   tdesktopPro\Telegram\build\prepare\win.bat silent
   ```
4. 配置并编译本体:
   ```
   cd /d D:\TBuild\tdesktopPro\Telegram
   configure.bat x64 -D TDESKTOP_API_ID=<你的api_id> -D TDESKTOP_API_HASH=<你的api_hash>
   cmake --build ..\out --config Release --parallel
   ```
   出现 `out\Release\Telegram.exe` 即说明服务器环境 OK。

> 这一步把依赖库落在了 `D:\TBuild\Libraries`,CI 之后会复用它,不必重编(见阶段 6)。

---

## 阶段 4 · 注册 self-hosted runner

1. 仓库 → **Settings → Actions → Runners → New self-hosted runner** → 选 **Windows / x64**。
2. 页面会给出一段命令(带一次性 token),在服务器上照做,大致如下:
   ```powershell
   mkdir C:\actions-runner; cd C:\actions-runner
   # 下载(用页面给的实际版本号)
   Invoke-WebRequest -Uri <页面给的下载地址> -OutFile actions-runner-win.zip
   Expand-Archive -Path actions-runner-win.zip -DestinationPath .
   # 配置(加一个自定义标签 winbuild)
   .\config.cmd --url https://github.com/<owner>/<repo> --token <页面给的TOKEN> --labels winbuild --name buildsrv-01
   ```
3. 配置过程中会问 **Run as service**,选 **Y**,把它装成 Windows 服务(开机自启、后台常驻)。
4. **服务账号权限**:确保跑 runner 的账号能用 `mklink`(阶段 1.1 的开发者模式已全局启用即可)。若你把服务改成了特定账号,给该账号"创建符号链接"权限或让其属于管理员组。

> 记住这个标签 **`winbuild`**,下一步 workflow 要用。

---

## 阶段 5 · 修改 `win.yml` 的 `runs-on`

当前(第 15 行)是:
```yaml
runs-on: ${{ matrix.arch == 'arm64' && 'windows-11-arm' || ((github.event_name == 'pull_request' || github.event_name == 'workflow_dispatch' || startsWith(github.ref, 'refs/tags/') || github.ref == 'refs/heads/nightly') && 'depot-windows-latest-32' || 'windows-latest') }}
```
把其中的 `'depot-windows-latest-32'` 换成你的 runner 标签数组:
```yaml
runs-on: ${{ matrix.arch == 'arm64' && 'windows-11-arm' || ((github.event_name == 'pull_request' || github.event_name == 'workflow_dispatch' || startsWith(github.ref, 'refs/tags/') || github.ref == 'refs/heads/nightly') && fromJSON('["self-hosted","windows","winbuild"]') || 'windows-latest') }}
```
> 用 `fromJSON('[...]')` 是因为三元表达式里要给出一个字符串数组标签。你只手动触发(`workflow_dispatch`)时会命中这条分支。arm64 那条走 `windows-11-arm` 托管机、你只编 x64,不用管。

---

## 阶段 6 · 让依赖库常驻,冷编只发生一次(自建机的最大价值)

`win.yml` 把 `Libraries` 建在 `%userprofile%\TBuild\Libraries`(不在会被 checkout 清理的工作区里),所以**同一台机器上它天然常驻**:`prepare.py` 会检查 `cache_keys`,已建好的库直接跳过。因此:
- **第一轮**:冷编依赖 ~2 小时。
- **之后每轮**:只重编 Telegram 本体(约 20–40 分钟,LTO 链接为主)。

无需依赖 `actions/cache` 上传下载几十 GB(那几步在 self-hosted 上仍能工作,但价值不大;先保持原样即可,后续想优化再说)。

---

## 阶段 7 · 触发并验证

- `win.yml` 是 `workflow_dispatch`(手动)。去仓库 **Actions → Windows → Run workflow** 手动触发。
- 观察是否落到你的 runner 上执行,第一轮成功产出 `Telegram.exe` 与 `tx64upd<版本>` 工件即完成迁移。

---

## 运维与安全要点

- **私有仓库**必须坚持;并在 Settings → Actions 限制谁能触发/改 workflow。
- **单机串行**:一台 runner 一次一个 job,多次触发排队;要并行就在同机或多机再注册几个 runner(注意共享 `Libraries` 会有写冲突,并行请各自独立目录)。
- **磁盘监控**:依赖 + 输出会占几十 GB,定期看剩余空间。
- **工作区清理**:self-hosted 不自动清 `_work`。构建异常后如遇脏状态,停服务、清 `C:\actions-runner\_work` 下对应仓库目录再重来(注意别误删 `%userprofile%\TBuild\Libraries`,那是你想保留的热缓存)。
- **服务升级**:runner 会自动更新;VS/SDK 版本别乱升,升级前确认仍匹配 `docs/building-win.md`。

---

## 常见失败点速查

| 症状 | 原因 | 解决 |
|---|---|---|
| `msvc-dev-cmd` 步骤失败 / 找不到工具集 | 14.44 或 SDK 10.0.26100 没装 | VS 安装器补装 |
| First set up 的 sed 步骤报错 | CMake 不在 `C:\Program Files\CMake` | 用官方 MSI 装到默认目录 |
| `nuget` / `python` 命令找不到 | 没加入 PATH | 补 PATH 后重开 runner 服务 |
| Prepare directories 里 mklink 失败 | 没开发者模式 / 账号无权限 | 启用开发人员模式 |
| 磁盘写满 | 空间不足 | 清理 / 扩容 |
