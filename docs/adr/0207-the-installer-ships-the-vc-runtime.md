# 0207 安装包随附 MSVC 运行库

- **状态**：accepted（2026-11-21）
- **背景**：Lens 动态链接 CRT（/MD），Windows 本身不带 vcruntime140 与 msvcp140；windeployqt 又以 `--no-compiler-runtime` 部署。1.0.0-alpha 与 1.0.0-beta 的 payload 因此不含任何 CRT DLL：装过 VC++ 可再发行组件包的机器没事，全新机器首次启动即缺 DLL 失败。
- **决定**：`installer/stage_crt.cmake` 从工具链出发（`CMAKE_CXX_COMPILER` 向上走到拥有 `Redist/MSVC` 的 VC 根），把最新 `Microsoft.VC*.CRT` 目录的全部 DLL 拷进 payload 根；找不到即 FATAL_ERROR，拒绝产出不完整安装包。不取 `windeployqt --compiler-runtime`：它缺 redist 时静默跳过，正是这次漏带的失败模式。便携版随 payload 一同获得。
- **后果**：安装包与便携版各增约 4-5 MB，换来全新机器可启动。CRT 清单随工具链升级自动走，无需手动维护；静态链接（/MT）要重新核对与 Qt 插件、sanitizer 构建的兼容性，不取。
