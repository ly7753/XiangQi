# XiangQi (象棋) - 高性能 Android 原生象棋

一款基于 Android C++ 原生开发的高性能中国象棋应用。项目采用 OpenGL ES 3.0 进行极致优化的渲染，并内嵌强大的“皮卡鱼 (Pikafish)” UCI 引擎（支持 NNUE 神经网络），为您提供极具挑战的 AI 对弈体验。

## ✨ 核心特性

* **🚀 极致性能架构**：核心逻辑、渲染引擎与音频系统全线采用 C++ 开发，通过 JNI 无缝对接 Android 顶层框架。
* **🧠 顶级 AI 引擎**：内置强袭皮卡鱼 (Pikafish) UCI IPC 外挂引擎，默认开启多后台线程与神经网络 (NNUE) 评估，支持随时切换红/黑方托管。
* **🎮 极客级渲染管线**：基于 OpenGL ES 3.0，采用自研 VBO 动态批处理 (Batching) 管线，界面渲染与滑动动画丝滑流畅。
* **🎵 超低延迟音效**：基于 Android AAudio，采用预计算正弦波表 (Wavetable) 彻底消除音频线程中的运算瓶颈，实现极低延迟的清脆落子反馈。
* **📜 严谨的规则判定**：内置完整的国标象棋规则检测，包括禁止送将、困毙判定、长将判负、常捉判和以及 120 步自然限着等。

## 🛠️ 编译与构建

本项目支持跨平台环境（如 Termux）或标准 Android Studio 进行构建。
* **快捷清理**：`python clean.py` （深度清理构建缓存与临时文件）
* **快捷构建**：`sh acs_build.sh` （包含编译、打包与自动唤起执行）

## 📄 License / 许可证

本项目因包含 Pikafish 引擎代码及衍生调用，整体采用 **GNU General Public License v3.0 (GPLv3)** 协议开源，详见 [`LICENSE`](LICENSE) 文件。
