# 0028 duty 对 QML 只读可见，不可构造

- **状态**：accepted
- **背景**：`AppController` 的 47 个 `Q_INVOKABLE` 里有 24 个只是把调用转给某个 duty，其中 `UpdateDuty` 占 10 个；QML 经门面访问的调用点有 146 处。读这个类的人要从大量转发里找出真正做事的部分。ADR 0013 的 “不把 duty 注册成 QML 类型” 原意是阻止 QML 构造 duty，而不是阻止 QML 看见它。
- **决定**：duty 可以作为门面上的只读属性暴露给 QML，例如 `Q_PROPERTY(lens::app::UpdateDuty* updateDuty READ updateDuty CONSTANT)`。duty 类以 `QML_ANONYMOUS` 与 `QML_UNCREATABLE("...")` 标注：QML 能读它的属性、调用它的 `Q_INVOKABLE`，但不能构造它。门面保留单例、信号和跨职责编排。本次只迁移 `UpdateDuty`，其余 duty 另行决定。
- **后果**：
  - 纯转发的 invokable 从门面删除，不留兼容层；QML 调用点在同一次改动中改为 `Controller.updateDuty.*`。
  - 门面上的 `update`、`download`、`updateCardVisible`、`autoUpdateCheck` 属性，以及 `updateChanged`、`downloadChanged` 信号删除，状态通知由 duty 自己发出。门面不再保留第二条访问路径。
  - 不是纯转发、且要发出门面信号的 invokable 留在门面，例如设置写入、导出、门控类调用：移进 duty 会把信号发射也带过去。`setAutoCheck` 只写 duty 自己的状态，所以迁移。
  - `QML_UNCREATABLE` 单独使用不够：它只设置不可创建标记，不注册类型，qmltyperegistrar 不会导出它，qmllint 报 `unresolved-type`。必须与 `QML_ANONYMOUS` 或 `QML_ELEMENT` 同用。同理，`Q_PROPERTY` 的类型要写全限定名 `lens::app::UpdateDuty`，否则 qmllint 按非限定名查不到。
