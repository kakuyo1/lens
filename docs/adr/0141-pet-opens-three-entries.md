# 0141 宠物对 QML 只开三个入口

- **状态**：accepted（2026-10-30）
- **背景**：ADR 0139 把仪式与闲逛交成 module 后，`PetController` 对 QML 仍有约 100 项接口：帧 5、场景 7、入场 1、picker 8、衣橱 3、窗口宿主 8，`PetScene.qml` 一份文件就要读十八个平铺属性。评审 01 把接口收缩列为余项，并要求 QML 只读一个 pose。
- **决定**：`PetController` 只保留三个 QML 入口，都是只读投影，状态仍由控制器一处写。`Pet.pose` 是 `PetPose`：figure（canvas、scale、layers、foot、墨迹、复习牌）、帧（action、frame、expression、sheets、anchors、worn、gaze）与场景（id、帧、sheet、shift、align、scale、layers、entry）一个值读完；figure 随换宠 / 改大小通知，帧随 `frameChanged`，眼神随 `gazeChanged`。`Pet.picker` 是 `PetPicker`：目录、拒绝包、当前宠物、明信片与包操作，确认请求从它发出。`Pet.host` 是 `PetHost`：窗口可见性、初始落点、贴边回收、落点保存、指针坐标、抓屏排除、帧时钟与减少动效。控制器原有的 C++ getter / 方法与信号保留，供进程内用例与组合根使用，不再挂 `Q_PROPERTY` / `Q_INVOKABLE`。
- **后果**：`PetScene.qml`、`PetWindow.qml`、复习牌、设置预览与扩展页都改读这三项，`SpriteLayer.qml` 改为必填 `canvas`，不再依赖单例。被排除的方案：用 `QVariantMap` 做 pose——每帧重建一份映射，且 `scale` / 墨迹这类静态值混在一起，会让复习牌的 `choose()` 每帧重算；把状态搬进三个投影——同一份帧被写两处。行为与像素不变，QML 用例的断言改为从新入口读。
- **ADR 关系**：0114 的 “duty 退为 adapter”、0139 的 module + adapter 继续适用于宠物；0101 的入场映射成为 `pose.entry`，0097 的注视仍是 `pose.gaze`。
