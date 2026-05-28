# Claude Skills

用 Claude Code 开发过程中积累的 skill 集合。每个 skill 解决一个实际问题。

## 已收录

| Skill | 说明 | 日期 |
|-------|------|------|
| [retry-clean](retry-clean/) | 构建/下载失败后自动定向清理，只删坏的，不动好的缓存 | 2026-05-28 |

## 目录结构

```
claude-skills/
  README.md
  retry-clean/
    SKILL.md              # Claude 看的指令
    README.md             # 人看的使用说明
    .claude-plugin/
      marketplace.json    # 插件市场元数据
```

## 使用方式

把 skill 目录放到项目的 `.claude/skills/` 下即可：

```bash
git clone https://github.com/graden11/claude-skills.git
cp -r claude-skills/retry-clean your-project/.claude/skills/
```
