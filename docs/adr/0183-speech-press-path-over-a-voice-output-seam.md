# 0183 发音按压路径走 VoiceOutput 接缝

- **状态**：accepted（2026-11-16）
- **背景**：发音的按下到出声——选档、兜底、缓存、播放——在 `SpeechDuty` 内部完成，而三类设备也由它自己构造：`QMediaPlayer` 与 `QAudioOutput`，按引擎名缓存的两台 `QTextToSpeech`，以及 sherpa 的 `LocalVoice` 加单线程池与五分钟空闲释放。`speech_test.cpp` 只钉纯规则与缓存；按压路径没有任何用例，因为无头 runner 一碰 `QMediaPlayer` 就没有声卡可答。这条路径又是发音文件里最常动的地方——近期五个功能提交（在线源选择、sherpa 音色、逐口音缓存、系统音优先、空闲释放）都落在它上面。
- **决定**：
  - 新增端口 `speech::VoiceOutput`（`src/speech/voice_output.h`）：`systemVoice(accent)`、`installed(voiceId)`、`say(word, accent)`、`play(bytes)`、`speakLocal(word, accent, voiceId, sink)`、`stop()`、`releaseLocalVoice(voiceId)`，外加 `onSoundState(playing, word)` 回调，报的是声音自己的开始与结束。接口只有 duty 真正要问的事实，不含 Qt Multimedia。
  - 生产适配器 `app::QtVoiceOutput`（`src/app/platform/qt_voice_output.{h,cpp}`）承接全部设备与它们的时序：播放器首次使用才建、缓冲区先解绑再销毁、`releasing_` 守卫、WinRT 先于 SAPI、本地引擎跨按压保留与空闲释放。适配器只执行，不选档：ADR 0134 的引擎与兜底规则仍是 `plan()` 的。它只编进 `lens` 可执行文件，不属 `lens_app`：设备代码与 `main.cpp` 同类，测试目标不链接它。
  - `SpeechDuty` 保留决定与状态：设置读取、`isHeadword`、`plan()`、按压先停、`requestId_` 过期守卫、在线缓存与取音、`playing` 投影，以及并列在旁的音色包下载管理。`removeVoice()` 先让输出释放本地引擎，再删文件。
  - 寿命：输出由组合根构建——`main.cpp` 的 `QtVoiceOutput`，qtest 的 setup 借一个无声实现——像 store 与 client 一样经 `AppController` 构造注入，并在控制器之前声明、之后销毁；duty 析构清空 `onSoundState`；本地合成的回调以 `QPointer` 守着 duty，迟到的应答落在空守卫上。
  - 测试面即接缝：`speech_duty_test.cpp` 用无声输出、脚本化网络与临时缓存目录驱动按压路径，九条用例覆盖词头闸、按压先停与图标随声、口音透传、兜底在线、缓存命中、掉卡后仍存不播、兜底关闭、本地合成回声与过期本地应答。
- **后果**：
  - 可听行为不变：同样三档引擎、同样的顺序与日志；设置的三个投影（`systemAvailable`、`systemVoice`、`localAvailable`）改为向输出要答案，语义不变。
  - 设备文件收拢：`QMediaPlayer`、`QTextToSpeech`、`LocalVoice` 只在 `qt_voice_output.cpp` 出现；该文件与 `main.cpp` 一起编进可执行文件，Multimedia 的播放器与 TextToSpeech 随之离开 `lens_app` 的链接行（Multimedia 因模式切换音效仍留），`src/app/CMakeLists.txt` 的注释同步。
  - 覆盖率门只裁 “扫描目标链接到的文件”：适配器不进任何测试二进制，便与 `main.cpp` 同列、不计入新增行分母；把它放进 `lens_app` 会让每个测试链接它却一次也执行不到，反而把设备代码变成覆盖率债。它的正确性因此靠人工听声，与重构前一致。
  - `stop()` 不再顺带清 `playing`，由 duty 自己清；引擎异步的 Speaking 仍经 `onSoundState` 重新点亮图标，与重构前一致。
  - 被排除：`upload_transaction` 式的纯按压步进机——真正没测到的是设备回调的编排，值进值出的状态机执行不了它；拆成播放器、系统、本地三道小端口——一个按压要三个替身才能驱动，而每个动词都只有一个调用方；把本地安装检查留在 duty——用例就得碰读者真实的 voices 目录。
  - 无头问题落定：按压路径进入日常离线套件。真实出声仍由人工验证——适配器没有用例，声卡不在测试机器上。
