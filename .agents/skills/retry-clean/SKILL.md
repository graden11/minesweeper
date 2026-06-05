---
name: retry-clean
description: Auto-cleans failed build/download artifacts and retries. Use when a build/download/install command fails, when disk space seems abnormally consumed, or when the user says "clean and retry", "retry-clean", "清理重试", "磁盘怎么满了", "下了X GB但占了Y GB", or runs --watch/--check. Two modes — A: auto-clean on failure, B: watch/check disk anomaly detection.
---

# retry-clean

When a build, download, or install command fails, partial artifacts accumulate and waste disk space. This skill performs **targeted cleanup** — only removing what is known to be incomplete or corrupt, never touching healthy caches.

## Cleanup Strategy Table

### Safe (auto-execute, no confirmation needed)

| Failure type | Cleanup command | Loses cache? |
|---|---|---|
| Docker build failed | `docker builder prune -f` | No — only dangling build cache |
| Docker pull failed | `docker image prune -f` | No — only dangling images |
| Docker compose pull failed | `docker image prune -f` | No |
| apt-get install failed (network) | `rm -rf /var/cache/apt/archives/partial/*` | No |
| pip install failed (network/download) | `pip cache purge <package>` | No |
| curl -O / wget download failed | Check for partial file at target path, delete if exists | No |

Note: `pip install nonexistent-pkg` fails with "not found" — no download happened, no garbage to clean. Only trigger cleanup when the error indicates a network/download failure (timeout, connection reset, incomplete read).

### Needs confirmation (diagnose automatically, ask before executing)

| Scenario | Command | Risk |
|---|---|---|
| git clone / git pull partial | `rm -rf <target-dir>` | Could delete user files |
| wget / curl -O partial file | `rm <target-file>` | Very low (file is corrupt anyway) |
| make compile interrupted | `make clean` | Loses .o files, requires recompilation (cmake cache kept) |
| Docker general bloat | `docker system prune` | Removes stopped containers + unused networks |

### Never do automatically

- `docker system prune -a` — removes ALL unused images including ones needed later
- `docker volume prune` — could delete database volumes
- `rm -rf build/` — loses cmake cache, requires re-running cmake
- `apt-get autoremove` — could remove system packages needed by other projects

## Mode A — Auto-clean on Failure (primary workflow)

This is the default mode. When a long-running build/download command is run with `run_in_background` and fails, Codex automatically triggers cleanup.

### Trigger keywords

`docker build`, `docker compose`, `docker pull`, `apt-get install`, `pip install`, `wget`, `curl -O`, `git clone`, `git pull`, `cmake`, `make`, `./build.sh`

### Behavior

1. Identify the failure type from the command that failed
2. If the failure is "package not found" or similar (no download attempted) → skip cleanup, no garbage
3. Execute the matching safe cleanup command **without asking for confirmation**
4. If cleanup fails (e.g. Docker daemon not running) → silently skip, do not retry cleanup
5. Report: what was cleaned, how much space was freed (or "Docker unavailable, skipped cleanup")
6. Ask only: "Retry?"
7. If the same command fails more than 3 times, automatically run `--check` logic for deeper diagnosis

### Important: only trigger on genuine download/build failures

Do NOT trigger cleanup when:
- `pip install nonexistent-pkg` → "package not found" (no download attempted, nothing to clean)
- `git clone` to a URL that doesn't exist → "repository not found" (no partial clone created)
- User is clearly running a test/verification, not actual development

DO trigger cleanup when the error indicates a download was in progress:
- Network timeout, connection reset, broken pipe
- "Failed to download", "incomplete read"
- Partial file or directory exists at the target path

### Example

```
Codex: Running docker compose up -d --build in background...
[2 min later]
Codex: docker compose build failed (exit code 1, network timeout)
        Auto-cleaned: docker builder prune -f → freed 2.1 GB (dangling build cache)
        Retry?
```

## Mode B — Watch / Disk Anomaly Detection

Two ways to trigger:

1. **Proactive**: `/retry-clean --watch` before a download, then `/retry-clean --check` after
2. **Reactive**: User says "downloaded 5GB but 7GB was consumed", "disk suddenly full", etc.

### --watch

Record baseline:
```bash
docker system df
df -h /var/lib/docker 2>/dev/null || df -h /
```

Output: "Baseline recorded. Docker Images: X GB, Build Cache: Y GB, Disk free: Z GB. Run your download, then /retry-clean --check."

### --check

Compare current state to baseline. Flag anomaly if actual increase > expected × 1.2.

If no baseline was recorded yet (no prior `--watch`):
- Tell the user: "No baseline recorded yet. Run /retry-clean --watch before the download, or tell me the expected download size and I'll compare against current state."
- If user provides expected size, compare to current Docker disk usage and flag anomalies

If baseline exists:
Analyze which categories grew:
- Normal: tagged images, application data
- Garbage: dangling build cache, dangling images, partial downloads

If anomaly detected → list cleanup options with safety labels
If normal → "All clear. Actual consumption matches expected."

## Long-Running Command Protocol

When the user asks to run any of these commands, always use `run_in_background`:
- `docker build` / `docker compose up --build`
- `./build.sh`
- `make -j$(nproc)`
- `cmake ..`
- `pip install` / `apt-get install`
- `wget` / `curl` downloading large files
- `git clone` large repos

Reasons:
1. Get notified immediately on completion — user never waits wondering
2. On failure, trigger Mode A cleanup instantly
3. User can continue other work

On failure notification:
1. Check exit code
2. Match failure type → execute safe cleanup
3. Report result
4. Ask "Retry?"

Never:
- Make the user discover failures themselves
- Stop and do nothing after a failure
- Ask "should I clean?" for safe cleanup items
