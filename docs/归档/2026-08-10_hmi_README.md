> ⚠️ **已归档 · 只读 · 不再维护**（归档于 2026-08-27）
> 原代码目录下的 README。内容已并入 `docs/现状.md`(它的阶段表是重构前最准的一版)。
>
> 当前状态看 [`docs/现状.md`](../现状.md) ｜ 接下来做什么看 [`docs/路线图.md`](../路线图.md)

---

# HMI — Qt 工业人机界面学习项目

> 个人学习项目：用 Qt（C++）从零实现工业 HMI（Human-Machine Interface，人机界面）。
> 目标场景：与 PLC（可编程逻辑控制器）通信、实时显示设备状态、接收操作指令。

## 🎯 项目目标

- 掌握 Qt 框架核心用法（信号槽、元对象系统、控件、布局）
- 实现工业现场常用 HMI 功能（数值显示、按钮、报警、变量订阅）
- 打通"上位机 ↔ PLC"通信链路（TCP / Modbus RTU/TCP）
- 形成可复用的 HMI 代码模板

## 📌 当前状态

**阶段 6 收官(6.4 完成,等 6.5 端到端验收),准备进入阶段 7。**

- ✅ 阶段 0 ~ 5 已完成(Qt 热身 → TCP → Modbus → 变量模型 → 控件绑定 → 业务层 + 报警联动)
- 🟡 阶段 6 进行中(6.1 schema ✅ / 6.2 saveToFile ✅ / 6.3 loadFromFile ✅ / 6.4 UI 按钮 ✅ / 6.5 端到端验收 ⏳)
- ⬜ 阶段 7 待开始(整合 demo + 真实 PLC + 24h 长跑 + README + 视频)

## 📚 学习路线

| 阶段 | 状态 | 内容 |
|---|---|---|
| 0 | ✅ | Qt 热身（窗口、信号槽、元对象系统） |
| 1.1 | ✅ | TCP 客户端（基础连接 + 收发） |
| 1.2 | ✅ | TCP 进阶（超时 + 重连 + 心跳 + 状态机） |
| 2 | ✅ | Modbus RTU/TCP（0x01-0x10 + 异常响应 + CRC） |
| 3 | ✅ | 变量模型（订阅发布 + 轮询 + 死区） |
| 4 | ✅ | 控件绑定（变量 ↔ UI） |
| 5 | ✅ | 业务层（状态机 + 报警 + 联动） |
| 6 | 🟡 | 工程文件（JSON 存读 + UI 打开/保存） |
| 7 | ⬜ | 整合 demo（真实 PLC + 完整框架 + 24h 长跑） |

## 🛠️ 编译与运行

**环境要求**
- Visual Studio 2022（v18+）
- Qt 5.15+ 或 Qt 6.x（Qt VS Tools 已配置）
- CMake 3.20+

**VS 打开：双击 `hmi.slnx` → 选择 `x64 / Debug` → F5**

**CMake 打开：**
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

## 📁 目录结构

```
HMI/
├── README.md                              # 本文件
├── LICENSE                                # MIT
├── .gitignore                             # Git 忽略规则
├── CMakeLists.txt                         # CMake 工程文件
├── hmi.slnx                              # VS 解决方案
├── hmi.vcxproj / .filters                # VS 工程文件
├── main.cpp                              # 程序入口
├── hmi.h / .cpp / .ui / .qrc             # Qt 主窗口
├── Variable.h / .cpp                     # 变量类（阶段 3）
├── VariableManager.h / .cpp              # 变量管理器（阶段 3）
├── ModbusMaster.h / .cpp                 # Modbus 主站（阶段 2）
├── ModbusDemo.h / .cpp                   # Modbus 测试 demo
├── TcpClient.h / .cpp                    # TCP 客户端（阶段 1.2）
└── 2026-07-06_ModbusMaster响应处理_知识点总结.md
```

## 🧱 关键模块

| 模块 | 文件 | 阶段 |
|---|---|---|
| TCP 客户端 | `TcpClient.{h,cpp}` | 1.2 |
| Modbus 主站 | `ModbusMaster.{h,cpp}` | 2 |
| 变量 | `Variable.{h,cpp}` | 3 |
| 变量管理 | `VariableManager.{h,cpp}` | 3 |

## 📝 学习笔记

阶段性踩坑、关键点已写到仓库根目录的 `YYYY-MM-DD_主题.md`，例如：
- `2026-07-06_ModbusMaster响应处理_知识点总结.md`

## 📄 License

MIT — 随便用、改、卖，保留原作者声明即可。