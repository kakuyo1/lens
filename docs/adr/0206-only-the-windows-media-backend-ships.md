# 0206 只随包发 Windows 媒体后端

- **状态**：accepted
- **背景**：Qt Multimedia 要选后端。FFmpeg 后端需要随包带约 17.5 MB 的 DLL，而应用对媒体的全部需求只是播放一个单词约 20 KB 的发音录音。
- **决定**：`selectMediaBackend()`（app/startup.cpp）在 `QApplication` 构造之前用 `qputenv` 设 `QT_MEDIA_BACKEND=windows`；环境变量已有值时不动，留给开发者覆盖。`installer/lens.iss` 只随包发 Windows 后端，不打包 FFmpeg DLL。
- **后果**：放弃 FFmpeg 后端的行为一致性，换来 17.5 MB 的安装包。改后端要同时改 installer 与这里的默认值。
