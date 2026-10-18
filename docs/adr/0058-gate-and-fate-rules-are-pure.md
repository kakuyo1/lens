# 0058 取词闸门与结果去留判定是纯函数，各自留在所属层

- **状态**：accepted
- **背景**：取词闸门（`CaptureDuty::captureRefusalReason()`）与 OCR 结果的去留判定（`pollOcr` 中的代次、窗口与故障分类）原先只在 Qt 槽里运行，单元目标走不到，只有真实 Tesseract 进程才能跑出来（ADR 0025 记过这一缺口）。拒绝原因枚举 `CaptureRefusal` 住在 `src/app/capture_refusal.h`（ADR 0025），而 `lens_capture` 位于 `lens_app` 之下，不能反向依赖 `app` 层。
- **决定**：闸门判定 `gateRefusal()` 与枚举同住 `src/app/capture_refusal.h`；结果去留 `ocrResultFate()` 与故障分类 `isRuntimeFault()` 放在 `src/capture/capture_policy.h`，只接收普通值，不依赖 Qt 窗口或 `app` 层类型。`CaptureDuty` 只收集输入、调用这些函数并执行 Qt 副作用。
- **后果**：闸门的测试在 `test/googletest/unit/app_seams_test.cpp`，去留与故障分类的测试在 `capture_test.cpp`。若将来 `CaptureRefusal` 迁入 `capture`，闸门也要一并迁移。被排除的替代方案是全部放进 `capture_policy.h`，那需要 `capture` 依赖 `app`。OCR 的第二个适配器是测试内的 `FakeOcr`，不进入产品代码。
