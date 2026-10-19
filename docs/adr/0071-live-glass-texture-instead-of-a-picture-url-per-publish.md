# 0071 幕布交给一张活纹理：每次发布替换纹理，不再换图片 URL

- **状态**：accepted（阶段 3；改写 ADR 0038 与 0039 的双槽 Image/ShaderEffectSource 做法，ADR 0040 的读取节奏不变）
- **背景**：阶段 2 把模糊搬进着色器后，剩下的第二道墙在交付：每次发布都换一次 `image://glass/<surface>/sheet?serial=N`，
  也就是每秒约 15 次跨线程图片加载、解码、纹理上传、mip 生成与 QML 图片缓存条目。双槽 Image 的做法正是为了掩盖这条
  异步路径的空档：新图加载进隐藏槽，就绪后才切换。15 次/秒还能靠它遮住，30/60 次/秒不可能。
- **决定**：
  - 新增 `GlassSheetItem`（`src/app/glass_sheet_item.h`）：一个 `QQuickItem`，听 `GlassSheet::pictureChanged`，把
    `picture()` 交给渲染线程，在 `updatePaintNode` 里用 `QQuickWindow::createTextureFromImage(..., TextureIsOpaque)`
    建纹理，装到 `QSGSimpleTextureNode` 上。新纹理装上的同一次渲染里，旧纹理还在节点上，所以发布本身不会空一帧；
    静止桌面被指纹门挡在发布之前，连上传都没有。
  - `GlassSurface` 用一个 `GlassSheetItem` 加一个 `ShaderEffectSource`（`mipmap: true`，供模糊读 mip）替掉两套
    Image + ShaderEffectSource。幕布矩形直接取 `GlassSheet.sheet`，与图片在同一次 `pictureChanged` 里更新，所以
    矩形与纹理在同一帧进入着色器，不需要每个槽位各存一份矩形。
  - `GlassSheetItem.active` 接 `drawsBackdrop`：玻璃关闭与直通时不持有纹理，重开时重新取当前幕布。
    `GlassHost` 仍为文字色保留那张幕布，与 ADR 0069 一致。
  - 图片服务删掉 `sheet` 路由，只留液体的位移图 `map`；serial 不再是缓存键，只作为测试与 state 的图片身份。
  - QML 模块加 `IMPORTS QtQuick`，因为注册给 QML 的类型现在以 `QQuickItem` 为基类。
- **后果**：
  - 应用内实测（同一 tally 场景，流光、真实排除抓屏）：`requests` 从每窗约 129 次降到**稳态 0 次**，首窗的 24 次是
    位移图在开窗时按尺寸重建；`image request ms` 之后为 `no sample`。`publishes` 仍为 129/10 s，读屏中位 13.1~13.5 ms。
    读屏价与交付路径无关，这个数比阶段 2 的 18.5 ms 低，落在本机日间散布（约 ±25%）与桌面活动差异之内。
  - 内存与纹理数下降：一个表面从两张幕布图加两块离屏纹理，变成一张幕布纹理加一块离屏纹理（后者本来就按窗口设备
    分辨率建）。原生幕布图约 2.25 MB，未单独复测进程占用。
  - 帧率仍未提到 30/60：67 ms 的 GDI 读取循环是下一道限制（阶段 4）。这条只是把交付从每帧一次图片加载改成一次
    纹理替换，让下一阶段有地方可提速。
  - 测试：`test_glassStaysOnScreenWhileItsBlurLoads` 换成 `test_glassKeepsItsLiveTextureWhileTheBlurMoves`，断言
    模糊移动不重取幕布、矩形不空、活纹理的 serial 不变；直通用例改断言 `drawsBackdrop` 与 `active`。离屏平台不渲染
    `ShaderEffect`，纹理是否真的上传由真机截图与 tally 证明，不能由 qtest 证明。
  - 未做：60 次/秒的长时间运行与丢帧统计；WARP/软件渲染下自定义 item 的表现；录制一段逐帧检查空帧的素材。
- **被排除的选项**：
  - 只改 URL 不换图（固定名字）：`QQuickImageProvider` 没有原地刷新信号，图片加载器仍会被调用；换 URL 是同步提供者
    唯一的强制重载办法（阶段 0 已记录）。
  - 保留双槽、只把加载改成同步：两套 Image 与两块离屏纹理仍在，且异步空档本来就消失了，槽位没有存在理由。
  - 直接用 `QSGTextureProvider` 让 ShaderEffect 采样自定义 item，省掉 ShaderEffectSource 的 FBO：少一次 GPU 拷贝，
    但要多写纹理提供者、动态纹理与 mip 的接线；当前每发布一次多一次整块 blit，代价远小于图片加载，先用简单的。
