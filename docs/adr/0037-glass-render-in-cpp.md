# 0037 玻璃的模糊、饱和与折射在 C++ 中计算，QML 只显示结果

- **状态**：accepted（取代 ADR 0034 中关于 `MultiEffect` 与流光着色器的决定；抓屏取背景的部分不变）。**部分被 ADR 0039 取代**：圆角与折射改由着色器逐帧计算，QML 不再只显示 C++ 处理好的整张图。
- **背景**：真机（Windows，Direct3D 11）上 `MultiEffect` 的模糊与饱和不生效，离屏渲染也不生效，带遮罩时结果是单色。原因未查清，是 Qt/RHI 的问题还是场景搭建的问题，两者未区分。离屏探针不能作为依据。流光的折射需要 `ShaderEffect` 与 `qsb` 编译的着色器，`qsb` 版本与运行时的对齐（ADR 0035）因此一直未定。
- **决定**：
  - 模糊、饱和、圆角遮罩与流光折射都在 `glass_math` 中于 C++ 计算。`GlassImageProvider` 按 `image://glass/frosted?…` 与 `image://glass/liquid?…` 输出已处理的图像；参数与抓屏序号写在 URL 里，Qt 的图片缓存按 URL 复用。
  - 模糊是三次盒式近似的高斯，标准差为模糊百分比 × 60 px（1x），按图像的设备像素比换算。饱和用 CSS `saturate()` 的矩阵。流光先模糊、后按位移图采样，与 ADR 0034 的顺序一致。
  - QML 只放一张 `Image`，填充与描边仍由 `GlassSurface.qml` 叠在上面。
  - 删除 `src/app/shaders/liquid.frag` 与 CMake 中的 `qt_add_shaders`，不再依赖 `qsb`。
- **后果**：
  - 每次参数变化，工作线程上都要重算整张图（设置窗口约 540 × 665 设备像素）。目标单次 ≤ 20 ms，见下方的测量。
  - 不再有 GPU 效果通道，所以 `MultiEffect` 在 Direct3D 11 上的不生效问题不再与玻璃相关。`MultiEffect` 仍被其他表面使用（阴影、图标、气泡），那些不在本决定之内。
  - 圆角与模糊半径乘以设备像素比，125% DPI 下不依赖 QML 对离屏贴图的重采样（`docs/QML.md` §3）。
  - 被排除的选项：继续调 `MultiEffect`（原因未知，无法保证）；保留着色器只把模糊移到 C++（折射仍要 `qsb`）；DWM 系统材质（ADR 0034 已排除）。
  - 视觉效果未在真机上核对。离屏只能验证数值与结构：`tst_phase2e` 的数值断言、`GlassMath` 的 gtest。
