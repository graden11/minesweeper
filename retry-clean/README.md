# retry-clean

构建/下载失败后自动清理残留垃圾，让你能放心重试。只删损坏和不完整的文件，不动健康缓存。

## 解决的问题

你跑 `docker compose up --build`，中途失败。修好问题重试，又失败。重试 5 次后发现磁盘满了——15 GB 全是失败构建留下的悬空 cache。

或者：下载一个 5 GB 的 Docker 镜像，断了几次重连，最后一看占了 7 GB。多出来的 2 GB 哪来的？

## 两种模式

| 模式 | 触发方式 | 行为 |
|------|---------|------|
| **A — 失败自动清理** | 构建/下载命令失败 | 自动跑安全清理（如 `docker builder prune -f`），报告释放了多少，问"要重试吗？" |
| **B — 磁盘监控** | `/retry-clean --watch` → `--check` | 下载前记录基线，下载后对比，发现异常增量就报警 |

## 安全分级

| 等级 | 示例 | 需要确认？ |
|------|------|----------|
| 🟢 安全 | `docker builder prune -f`、`docker image prune -f`、`apt-get clean` | 否，自动执行 |
| 🟡 需确认 | `rm -rf <不完整的 clone>`、`docker system prune` | 是 |
| 🔴 禁止 | `docker system prune -a`、`docker volume prune`、`rm -rf build/` | 永远不做 |

## 安装

```bash
mkdir -p .claude/skills
git clone https://github.com/你的用户名/retry-clean.git .claude/skills/retry-clean
```

## 用法

### 模式 A（自动，不需要配置）

正常开发就行。构建失败时 Claude 自动清理并问你要不要重试。

原理：skill 教 Claude 用 `run_in_background` 跑长构建，完成后自动检查 exit code，失败就匹配清理策略。不依赖 hook，不会误触发。

```
Claude: docker compose build 失败 (exit code: 1, 网络超时)
        自动清理：docker builder prune -f → 释放 2.1 GB
        要重试吗？
```

### 模式 B（手动监控）

```
/retry-clean --watch           # 记录基线
docker pull large-model:latest
/retry-clean --check            # 对比，发现异常
```

## 为什么不靠 hook

尝试过 `PostToolUse` hook，但 Claude Code 的 hook matcher 只能匹配工具名（如 `Bash`），无法区分 `docker build` 和 `ls`。结果是每次 Bash 都触发，频繁打断操作。skill 级别的行为指令更可靠。

## 依赖

- Claude Code
- Docker（Docker 相关清理功能需要）

## License

MIT
