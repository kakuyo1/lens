# 0204 No new surfaces or features before 1.0.0 stable

- **Status**: accepted (2026-11-20; the closing decision of the memory-optimization campaign)
- **Background**: Stage 1 measured what the app costs at rest and showed that a surface is not free. One glass surface ignites Windows Graphics Capture and, until ADR 0202, held a fixed +195 MB private floor; even after the session release and the shared-device trim each ignition leaves a +12.5 to +16.9 MB driver high-water at hide+60 s (ADR 0203). The app carries 19 surfaces, each with QML, duties, translation strings and test rows; every addition widens the area that must be measured, tested and translated. The campaign exists to close cost, not to add more of it.
- **Decision**:
  - No new surface and no new feature before 1.0.0 stable. The freeze is the default; the memory-optimization campaign was its only planned scope and is closed.
  - A surface added after 1.0.0 must meet the resource budget the campaign set: idle private memory ≤ 335 MB, the capture session released within 15 s of the last glass sheet hiding, hide+60 s private memory ≤ idle +25 MB, and a handle slope below 0.5 over 100 open/close rounds. It carries its test row and an ADR of its own.
  - An exception is made by an ADR, not by review; without one the answer is no.
- **Consequences**:
  - Reader requests that add a surface or a feature wait for 1.0.0. Recorded interest is an issue, not a roadmap promise; Linux support is the first.
  - Review gains a standing no: "small and useful" no longer outweighs the surface it adds.
  - The freeze ends at 1.0.0 stable, not on a date, so no date is promised.
- **Alternatives rejected**:
  - **Unbounded growth** (the status quo): 19 surfaces, each measured, tested and translated, while the next surface outruns the budget the campaign just set.
  - **Freezing forever**: reader interest does not stop at 1.0.0; the budget, test row and ADR path reopen growth without reopening the default.
