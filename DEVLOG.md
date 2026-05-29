# Claude Code 开发日志

记录使用 Claude Code 开发过程中踩的坑、写的 skill、解决的问题。

---

## 2026-05-28 — retry-clean skill 开发

### 背景

磁盘频繁爆红。`docker build` / `pip install` / `git clone` 失败 → 重试 → 残留垃圾累积 → 发现时已多十几 GB。

### 需求

写一个 Claude Code skill：失败后自动定向清理、报告释放量、问要不要重试。**只删坏的，不动好的。**

### 踩坑

#### 坑 1：PostToolUse hook 误触发

用 `"matcher": "Bash"` 拦截失败命令。但 hook matcher **只能按工具名过滤**，不能按 exit code 或命令内容区分。结果是 `ls`、`mkdir`、`echo` 全触发，频繁打断操作。

**结论**: 废弃 hook。改用 skill 行为指令（`run_in_background` + 失败检测）。

#### 坑 2：CronCreate 打断对话

用 `CronCreate` 每 30 分钟跑清理。但 cron 触发后**以 prompt 形式插入当前对话**，每半小时中断一次。没有后台静默机制。

**结论**: Claude Code cron 只适合"定时提醒"，不适合后台任务。改用 WSL 原生 cron。

#### 坑 3：架构变更导致测试用例过时

验证方案 29 个用例以 Docker 为主。中途架构从"全 Docker"变成"只有 MySQL 在 Docker"，Docker 用例被迫降级，pip/wget/git 升为主力。

**教训**: 验证方案要和实际架构同步。开发/生产环境分离后，测试重点随之变化。

### 最终方案

三个模式：

**A. 失败自动清理**（skill 指令驱动）
- 长命令用 `run_in_background`
- 失败 → 匹配清理策略 → 🟢 安全项自动执行 → 报告 → 问"要重试吗"
- 不依赖 hook，不误触发

**B. 磁盘监控**（手动触发）
- `/retry-clean --watch` 记录基线
- `/retry-clean --check` 对比异常
- 事后追问"磁盘怎么满了"也触发

**C. WSL 环境管家**（独立于 Claude Code）
```
0 2 * * * echo 3 > /proc/sys/vm/drop_caches           # 每日释放 WSL 内存
0 3 * * * find /tmp -type f -mtime +7 -delete          # 每日清理 /tmp
0 4 * * 0 docker system prune -f                       # 每周清理 Docker
0 5 1 * * apt-get clean                                # 每月清理 apt
```

### 关键设计决策

1. **安全三级**：🟢 自动不问 → 🟡 诊断后问 → 🔴 永远不做
2. **Hook 零依赖**：skill 的行为指令比 hook 更可靠、更精确
3. **WSL cron > Claude Code cron**：独立进程不打断对话
4. **pip 不进 cron**：开发时缓存命中率高，手动清理更合理

### 产出文件

- `.claude/skills/retry-clean/SKILL.md` — skill 指令
- `.claude/skills/retry-clean/README.md` — 使用说明
- `.claude/skills/retry-clean/.claude-plugin/marketplace.json` — 插件市场元数据
- WSL crontab — 4 条定时清理任务
- `.claude/settings.json` — hooks 清空（hook 方案已验证失败）

### 待完成

- [ ] Docker 在线后跑完 25 个验证用例
- [x] 发布到 GitHub 仓库 — https://github.com/graden11/claude-skills
