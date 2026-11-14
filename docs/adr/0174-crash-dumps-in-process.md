# 0174 崩溃转储随崩溃写进日志目录，下次启动带出通知

- **状态**：accepted
- **背景**：崩溃后只剩日志可查。2026-11 的多起 0xC0000005（ADR 0167、ADR 0168）靠手动配置 WER LocalDumps 指到 `logs/dumps` 才完成取证，这份配置不随产品走，换一台机器就没有。项目没有遥测与上传基础设施，SECURITY.md 的边界是一律不自动上传。
- **决定**：
  - 进程内 SEH 未处理异常过滤器加四个 CRT 漏斗（terminate、pure call、invalid parameter、SIGABRT），汇入同一个写转储路径；dbghelp 以静态导入随进程加载，崩溃路径不再加载任何库。
  - 转储写日志目录下的 `dumps\`：与当初 WER 取证的位置一致，开发树已在 `.gitignore` 内，安装版随 `{app}\logs` 被 `lens.iss` 的 filesandordirs 一并清除，安装器零改动。
  - 每次崩溃写三样：`lens-<版本>-<时间戳>-<pid>.dmp`、同名 `.crash.txt` 摘要（版本、时间、异常码、地址、模块；无用户文本）、`last-crash.txt` 标记；标记在下次启动的通知展示后删除，所以一次崩溃只通知一次。
  - 通知复用 UI-SPEC §4.5 通知卡片，两枚胶囊：打开转储文件夹、报告问题（预填 GitHub issue，发送需读者点击）。转储常开、无开关，与日志同待遇；保留最近 5 份，启动时修剪。
  - 默认 `MiniDumpNormal`（栈与模块表，配构建树的 PDB 定位）；`LENS_CRASH_FULL_DUMP=1` 才带间接引用内存，供按需的深挖轮。
- **后果**：
  - 转储含进程内存，可能带正在读的文本乃至密钥字节——SECURITY.md 如实声明；`MiniDumpWithFullMemory` 永不进默认路径。
  - 进程内写转储在堆损坏时可能失败；失败也写摘要与标记，取证退回日志。
  - 不引 Crashpad/Breakpad（独立的 handler 进程加第三方代码，对这个产品过重），不写 WER 注册表（机器侧配置，不随产品走）。
  - 纯逻辑（目录、文件名、摘要往返、保留修剪）在 `src/util`，离线测试进 `lens_gtest_unit`；QA 用 `LENS_CRASH_TEST=1` 走一次真实崩溃，回路见 `docs/qa.md`。
