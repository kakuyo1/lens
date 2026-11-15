# 0179 CI 加 clang-tidy 与 ASan 两道闸

- **状态**：accepted
- **背景**：CI 有五个 job 但没有静态分析和动态分析——上周 capture 的空句柄解引用（ADR 0175 的 0xC0000005）正是这两类工具的目标类别。`build-ninja` 的 compile_commands.json 早已在产出，只是没人读它。
- **决定**：
  - clang-tidy 是 CI 专属闸，不进 pre-commit（要配置好的树、分钟级，违背 hook “mechanical and cheap”）。范围与 hook 同一哲学：只查这次改动碰到的源文件，头文件经由包含它的 TU 进入。gate 是零基线——`.clang-tidy` 里的检查清单由 267 个 TU 的全树普查按数据收窄：今天为零命中的全收（未来的命中都是新代码，闸红在带来它的那个 PR 上），有存量的按命中数记在 `.clang-tidy` 注释里逐个挣回，清单只涨不缩。
  - clang 读不了 MSVC 格式的 PCH，所以 tidy 的树必须以 `CMAKE_DISABLE_PRECOMPILE_HEADERS=ON` 配置（configure-only，从不构建；CI 在 configure action 后一次重配置，本机配方是脚本自己打印的 `build-ninja-tidy`）。这件事顺带暴露了一类真实卫生问题：头文件靠 PCH 才自包含（page_reader_uia.h 缺 `<QtGlobal>` 即此）。
  - 七个 TU 路径 clang 在这对 Qt/MSVC 组合下解析不了，tidy.sh 里 `clang_skips` 具名跳过并附原因——五个是第三方头的问题（SDK 的 directxmath.h 包了 clang-libc 的 `<cpuid.h>`；Qt 的 qplugin.h 在 constexpr 里算 offsetof），两个是 QTest 目标结尾的 `.moc` include 只有构建后才存在而 tidy 树从不构建。显式跳过，不是静默；Qt/SDK 升级让它们能解析后自动回到闸里。
  - sanitizer job 复用 unit 布尔，配置为 **RelWithDebInfo + `/fsanitize=address`**。最初选的是 Debug 树（保住断言与 TRACE），但 Debug CRT + Debug 版 Qt DLL + ASan 三者在 runner 上连踩三雷——每次崩溃点都不同、签名稳定、靠给测试 main 加 SIGABRT/terminate/未处理异常三层处理器加 PDB 离线符号化才逐一定位：`_ITERATOR_DEBUG_LEVEL=2` 的 `_Orphan_all` 代理链在每次 pet-parse throw 展开时故障（IDL0 是官方对 /fsanitize=address 的配套）；旗标覆写吞掉了默认 `/EHsc`；最后一个是 Qt6Cored.dll 里 memcpy 越过插桩帧红区的栈溢出读。RelWithDebInfo 与 Qt release DLL 同用 /MD，Debug 专属机制按构造消失——代价是 NDEBUG 关掉断言，断言行为由 unit job 的 Debug 运行继续覆盖。MSVC 工具链下没有 UBSan，clang-cl 换编译器换来的不值得一次 shakedown——只做 ASan。Qt 与 sherpa-onnx 运行库不插桩没关系：ASan 运行库钩的是进程级分配器。
  - 泄漏检测（LSan）不作为闸：MSVC ASan 在 Windows 上没有 clang 语义的 `detect_leaks` 保障，qtest 的 Qt 分配噪声也未度量——先让内存错误闸跑起来。
  - 故意行为以 NOLINT 声明故意（crash_dump.cpp 的合成空指针写、card_session.cpp 两处先拷贝再 erase——检查建议的引用会悬垂）；glass_capture 的三个 winrt 空 catch 由 `AllowEmptyCatchForExceptions` 按类型放行。随之落地的真修复：`%08X` 对 DWORD、`~GlassDuplication` 缺 `override`、FARPROC 经 `void*` 的多余一跳、OCR 结果在 move 形状下被再读（提为局部量）、scripted_manager.h 与 page_reader_uia.h 依赖 PCH 才自包含的两处缺 include、pet_scenes 一处多余的拷贝、三处测试循环的拷贝与扩容，以及 TempPricing 共用一个临时路径且析构用可抛 `std::filesystem::remove`（CI 上撞实时扫描器的 sharing violation，整个套件 terminate）。
- **后果**：
  - gate 增两个 job，都受变更分类布尔门控，QML-only 与文档改动不触发；tidy job 只 configure 不构建（两个 configure 各十秒），sanitizer job 的 RelWithDebInfo+ASan 构建与其它 job 并行不占关键路径。
  - 本地想跑 clang-tidy 的机器要留一个从不构建的 `build-ninja-tidy`；CI 镜像自带 `Microsoft.VisualStudio.Component.VC.ASAN` 与 LLVM，本机 VS 未装 ASan 组件，装上后才能在本地复现 sanitizer job。
  - 被排除的检查清单是显式的欠账，不是终局：修完存量的那天，把对应检查从 `.clang-tidy` 的排除行里删掉即回到闸内。
  - 测试 main 为 lens_gtest_unit 自备（CRT 报告走 stderr、SIGABRT/terminate/未处理异常三层处理器），ASan job 失败时上传该次构建的 exe 与 PDB 供离线符号化——ASan 对硬故障只给模块偏移，进程内 dbghelp 在死亡路径上读不了 PDB，离线映射是唯一可靠路径。
