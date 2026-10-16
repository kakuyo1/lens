# 0047 未签名的安装包配一份免安装 zip

- **状态**：accepted
- **背景**：`Lens-1.4.0-setup.exe` 没有 Authenticode 签名，Windows Defender SmartScreen 因此在
  读者双击它的时候弹出 “Windows 已保护你的电脑”，点 “更多信息” 才能看到 “仍要运行”。SmartScreen
  拦的是 “没有签名、下载量不足的新程序”，不是这份文件有问题——但读者看到的是一个蓝框，第一反应
  是这个软件不安全，而实际上唯一缺的是签名。
- **决定**：不为此立刻买证书，先做三件成本为零或极低的事，把弹窗从 “拦路” 降级为 “一次点击” ——
  1. **免安装 zip**：把安装包要装进 `{app}` 的那份内容（payload + `data/`）原样打成
     `Lens-<版本>-portable.zip`，和安装包一起发在同一个 Release 里。读者解压即用，不经过 SmartScreen
     盯得最紧的安装器。
  2. **README 明说**：四份 README 各写一节安装说明，讲清楚为什么会有这个弹窗、点哪两下、以及怎么用
     `SHA256SUMS` 自己核对文件没被改过。
  3. **校验和覆盖两个包**：`SHA256SUMS` 同时列出安装包和 zip 的摘要。更新通道本来就只认自己点名的那
     一个文件（`src/update/update_pure.cpp`），多出来的行它会跳过而不是拒绝，所以一份清单够用。
- **后果**：
  - **zip 不是签名的替代品**。解压出来的 `lens.exe` 仍带 Zone.Identifier（Mark-of-the-Web），双击
    一样会弹 “打开文件 - 安全警告” 那一档，只是那一档只问一次，而不像安装包那样每次都拦。这个 zip 买的是
    “不用装”，不是 “不被拦”。
  - **zip 没有开始菜单快捷方式和卸载项**。它就是一个目录；读者自己决定放哪、自己删。自动启动那条
    Run 项是程序按自己的路径写的（`src/app/autostart.cpp`），所以解压到任何目录都仍然正确。
  - **两份包会漂移**。因此 zip 由 `installer` 这一个 CMake target 出（和 Inno Setup 同一个 target），
    不是在发布脚本里另做一遍；两者内容相同这件事由构建保证，而不是靠人记得同步。
  - **CI 的 `hygiene` job 不覆盖它**：`publish-release.ps1` 只有人手动跑，真跑失败就是发布失败，
    没有自动门禁。
  - **真正的解法仍然是签名**。OV 证书（约 $70–200/年）或 Azure Trusted Signing 拿到之后，
    `signtool` 签 `lens.exe` 与安装包，配合 GitHub Releases 积累下载量，SmartScreen 会自己闭嘴。
    那时候本 ADR 不必废弃——zip 作为 “给不想装东西的人” 的选项可以留下；但签名要加在
    `installer` target 之后半段，和 zip 一起，而不是取代它。