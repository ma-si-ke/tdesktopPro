# 更新公告撰写 Runbook

> **适用范围**：为 fork 的新版本撰写面向员工的中文更新公告,内容来自上游官方(或自有)提交历史。
>
> **首次实践**:7.0.1 版本公告(覆盖官方 6.9.2 → 7.0.1 共 808 个提交,产出 702 条,产物见仓库根目录 `更新公告-7.0.1.txt`)。
>
> **第二次实践**:7.1.5 版本公告(覆盖官方 7.0.1 → 7.1.4 共 1500 个提交,产出 1142 条,产物 `更新公告-7.1.5.txt`)。补充规则:同一范围内被 Revert 的提交与其 Revert 一起 skip(晚于 Revert 重新提交的同名提交照常保留);上游 canary 通道专属的展示/构建提交 skip;仅 support 账号可见的改动 skip。

---

## 0. 硬性规则

1. **每个 commit 单独一条,严禁合并**。同 subject 重复出现的提交也各写一行,用"进一步调整""继续完善"区分。
2. **与用户无关的提交不写**(过滤规则见 §2)。
3. **拿不准是否用户可见时,倾向保留**。
4. 统计数字(总数、各分类条数)**必须由脚本从数据算出**,不许手数。
5. 编号全文连续(1 开始,跨节不重置)。

---

## 1. 确定提交范围并导出

范围 = 上一次公告的基线版本 → 本次发版的基线版本。用官方 `Version X.Y.Z.` 提交定位端点:

```bash
git log --format="%h %ad %s" --date=short upstream/dev | grep -E " Version [0-9]" | head
git rev-list --count <旧版本commit>..<新版本commit>
git log --reverse --format="%h|%s" <旧>..<新> > /tmp/commits.txt
```

---

## 2. 过滤规则(skip 类,不进公告)

- 版本号提交(`Version X.` / `Beta version ...`)
- API scheme 更新(`Update API scheme ...`)
- 子模块更新(`Update lib_*` / `Update submodules` / TooManyCooks / cmake_helpers / breakpad / tgcalls 等)
- 构建修复(`Fix build with ...` / `Fixed build` / codegen / linker / cmake policy)
- CI / Docker / snap / flatpak / cache / actions bump
- `[ai]` 开头的提交(上游的 agent 规则文档)
- README / devcontainer / clangd
- 纯内部重构(重命名、`Move X to separate module`、`Remove dead/unused`、`Get rid of`、`Extract reusable`、`Prune unused`、`Removed N unused style entries` 等无用户可见效果的)
- `Update User-Agent for DNS ...`
- 仅日志类(`Added logs of ...`)、lang keys build、读内部配置标志(`Read config flag ...`)
- **非 Windows 平台专属**(fork 只发 Windows,只保留 Windows 专属和跨平台生效的内容):
  - macOS / Mac:`now playing`、Cocoa、location picker on macOS、`docx` IP 警告、Xcode、MAS、unfocused macOS window 等
  - Linux:Wayland、X11、snap、flatpak、GTK / GTK-3、webkitgtk、xcb、`web.telegram.org` 无法访问时的 webview 回退等
  - 跨平台改进(不含平台字样、或对所有平台生效的功能/修复)照常保留。

---

## 3. 分类体系

三大节 + 主题子分组:

- **sec**(安全加固):数据校验、尺寸检查、路径检查、IP 泄露警告、safeguards / guardrails / Validate / Harden 类
- **fix**(Bug 修复):`Fix/Fixed` 开头且非构建修复;崩溃修复也算。**属于新功能自身的修复也归这里**(与官方公告习惯一致)
- **功能/改进主题**:按当次版本的实际内容定主题集。7.0.1 用过的主题(可复用、可增删):
  `community`(社群)、`richmsg`(富文本消息/文章编辑器/IV/公式/Markdown查看器)、`ai`(AI 创作)、`ephemeral`(临时消息)、`chattheme`(聊天主题/外观)、`profile`(资料页)、`viewer`(媒体查看器/播放)、`gesture`(手势/滚动)、`search`(搜索)、`contacts`(联系人/聊天列表)、`stickers`(贴纸/表情)、`system`(系统集成)、`a11y`(无障碍)、`perf`(性能)、`export`(导出)、`grams`(Grams)、`uiops`(界面与操作,杂项兜底)
  7.1.5 新增的主题:`ttlmedia`(阅后即焚媒体)、`anim`(动效与渲染:thanos、qrhi)、`mediaeditor`(图片/视频编辑器)、`chat`(聊天界面)、`poll`(投票)、`ton`(TON、星星与礼物)、`business`(商业功能)、`login`(登录与通行密钥)、`calls`(通话)、`stories`(动态)

---

## 4. 翻译风格与译名表

风格:简洁一句话,句末"。"。新功能用"新增/支持/添加",改进用"改进/优化/调整",修复用"修复"。

译名表(增补时同步更新此表):

| 英文 | 中文 |
|---|---|
| community | 社群 |
| rich message | 富文本消息 |
| IV editor | 文章编辑器 |
| pullquote | 引述块 |
| ephemeral message | 临时消息 |
| pull-to-next / swipe-next-channel | 滑动切换下一频道 |
| chat theme | 聊天主题 |
| shared media | 共享媒体 |
| dialogs / chat list | 聊天列表 |
| peer list | 名单列表 |
| screen reader | 屏幕阅读器 |
| markdown viewer | Markdown 查看器 |
| formula | 数学公式 |
| pending requests | 待处理申请 |
| hidden chats | 隐藏聊天 |
| userpic | 头像 |
| collage / slideshow | 拼贴 / 幻灯片 |
| spoiler | 剧透遮罩 |
| draft | 草稿 |
| compose | 输入框 |
| toast | 提示 |
| badge | 角标 |
| voice message | 语音消息 |
| taskbar | 任务栏 |
| guard bot | 守护机器人 |
| footer | 脚注块 |
| passkey | 通行密钥 |
| welcome message | 欢迎消息 |
| offer | 报价 |
| TTL media / self-destructing media | 阅后即焚媒体 |
| thanos effect | 消散动画 |
| img-editor / vid-editor | 图片编辑器 / 视频编辑器 |
| rich paste | 富文本粘贴 |
| kinetic scroller | 惯性滚动 |
| Stars / gift | 星星 / 礼物 |

---

## 5. 流水线(大批量时)

提交数多(>100)时按 ~200 条一段并行分类翻译,产出 TSV:

```
hash<TAB>分类<TAB>中文一句话(skip 留空)
```

**合并后必须校验**:

```bash
cat class_part*.tsv > merged.tsv
wc -l merged.tsv                                   # = 输入行数
diff <(cut -f1 merged.tsv) <(cut -d'|' -f1 commits.txt)   # hash 顺序一致
awk -F'\t' '$2!="skip" && $3==""' merged.tsv       # 非 skip 不许缺译文
cut -f2 merged.tsv | sort -u                       # 无未知分类
```

生成最终公告用脚本按"主题顺序分组 + 全文连续编号"输出,统计行由数据算出。7.0.1 用的生成脚本(awk)思路:按固定主题顺序遍历 → 每主题打小节标题 → 逐条编号 → 最后 fix、sec 两节 → 头部统计行用 `total/feat/fix/sec` 变量填充。

---

## 6. 公告格式

```
X.Y.Z 版本更新
本次更新共 N 项修改,其中 A 项新功能/改进,B 项 Bug 修复,C 项安全改进

本次更新不需要重新登录。

  新功能 / 改进

  <主题小节名>
  1. ...
  2. ...

  <下一个主题>
  3. ...

  Bug 修复

  100. ...

  安全加固

  200. ...
```

**"是否需要重新登录"的判定**:看 `CreateLocalKey` 的 fork salt 是否变化(参见 tdata fork isolation 相关提交)。salt 没动 → "本次更新不需要重新登录";动了 → 明确写"本次更新需要重新登录"。

---

## 7. 交付

- 产物放仓库根目录:`更新公告-X.Y.Z.txt`(纯文本、UTF-8 无 BOM,直接复制发布)
- 发版时 GitHub Release notes 只放一段摘要(总数 + 三大类数字 + 主要主题),全文走内部公告渠道
