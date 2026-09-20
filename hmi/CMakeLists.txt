cmake_minimum_required(VERSION 3.21)
project(hmi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_PREFIX_PATH "D:/Qt/6.8.3/msvc2022_64")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Widgets PrintSupport SerialBus Sql)

qt_standard_project_setup()

# 显式列出全部源/头文件 —— 任何新增都必须更新这里
set(SOURCES
    main.cpp
    hmi.cpp
    TcpClient.cpp
    ModbusDemo.cpp
    ModbusMaster.cpp   # 阶段 2 第 4 课补图 —— 不加进 SOURCES 会导致链接错误
    Variable.cpp       # 阶段 3.1:Variable 数据模型
    VariableManager.cpp # 阶段 3.1:变量管理器
    PollScheduler.cpp   # 阶段 3.3:定时轮询调度器
    Binding.cpp         # Binding 控件绑定
    WriteScheduler.cpp  # 写入调度
    ActionManager.cpp   # 动作映射
    Alarm.cpp           # 告警
    AlarmManager.cpp    # 告警管理器
    ThresholdAlarmChecker.cpp # 阈值告警检查
    WriteFailChecker.cpp # 写入失败检查
    JsonSerializer.cpp  # JSON 序列化
    Project.cpp         # 项目(保存/加载)
    # ★ D.2(2026-09-20):QCustomPlot 曲线控件(LGPL v2.1,3rdparty/qcustomplot/)
    3rdparty/qcustomplot/qcustomplot.cpp
    # ★ E.1(2026-09-20):控件基类 + 第 1 个显示控件 LabelControl(QGraphicsObject 派生)
    src/widgets/base/ControlBase.cpp
    src/widgets/LabelControl.cpp
)

set(HEADERS
    hmi.h
    TcpClient.h
    ModbusDemo.h
    ModbusMaster.h
    Variable.h
    VariableManager.h
    PollScheduler.h
    Binding.h
    WriteScheduler.h
    ActionManager.h
    Alarm.h
    AlarmManager.h
    ThresholdAlarmChecker.h
    WriteFailChecker.h
    JsonSerializer.h
    Project.h
    # ★ D.2:QCustomPlot 头文件
    3rdparty/qcustomplot/qcustomplot.h
    # ★ E.1:控件基类 + 第 1 个显示控件 LabelControl
    src/widgets/base/ControlBase.h
    src/widgets/LabelControl.h
)

set(HEADERS
    hmi.h
    TcpClient.h
    ModbusDemo.h
    ModbusMaster.h
    Variable.h
    VariableManager.h
    PollScheduler.h
    Binding.h
    WriteScheduler.h
    ActionManager.h
    Alarm.h
    AlarmManager.h
    ThresholdAlarmChecker.h
    WriteFailChecker.h
    JsonSerializer.h
    Project.h
)

set(FORMS
    hmi.ui
)

set(RESOURCES
    hmi.qrc
)

# ★ 显式列表 + GLOB 兜底(防"加了 .cpp 忘改 CMakeLists.txt"再次踩坑)
#   - 显式列表里的文件永远编(GLOB 不会重复加)
#   - 新增的 .cpp / .h 自动捡进 SOURCES / HEADERS
#   - CONFIGURE_DEPENDS 让 CMake 在 build 时检测文件变化,自动重 configure
#   - 排除 Qt 生成的中间文件(*.moc / moc_*.cpp 等)
# ★ 阶段 A 修复:排除 tests/ 目录(它有自己的工程和 moc 文件,不能混编)
file(GLOB_RECURSE EXTRA_CPP CONFIGURE_DEPENDS
    "*.cpp"
    "src/*.cpp"
)
file(GLOB_RECURSE EXTRA_H CONFIGURE_DEPENDS
    "*.h"
    "src/*.h"
)
# 排除 tests/、x64/、out/ 等构建产物目录
list(FILTER EXTRA_CPP EXCLUDE REGEX "[/\\](tests|x64|out)[/\\]")
list(FILTER EXTRA_H   EXCLUDE REGEX "[/\\](tests|x64|out)[/\\]")
list(REMOVE_ITEM EXTRA_CPP ${SOURCES})
list(REMOVE_ITEM EXTRA_H   ${HEADERS})
list(APPEND SOURCES ${EXTRA_CPP})
list(APPEND HEADERS ${EXTRA_H})
# ★ 给 VS 解决方案资源管理器分组(只在 VS 里生效,Xcode/CLion/Ninja 不受影响)
#   CMake 用 source_group() 声明逻辑组,VS 会按这些组在"CMake 目标视图"里建子文件夹
#   匹配规则:对每个文件按顺序找第一个匹配的组,所以 Sources 必须在 Headers 之前
source_group("Sources"     REGULAR_EXPRESSION ".*\.cpp$")
source_group("Headers"     REGULAR_EXPRESSION ".*\.h$")
source_group("Forms"       REGULAR_EXPRESSION ".*\.ui$")
source_group("Resources"   REGULAR_EXPRESSION ".*\.qrc$")
source_group("Build Files" REGULAR_EXPRESSION "(CMakeLists\.txt|.*Presets\.json|.*\.slnx|.*\.vcxproj.*|default\.json|LICENSE)$")

# 阶段 A:Debug + Release 都用 Windows 子系统
# 启动没黑框,用户没法通过关黑框误杀程序;qDebug 输出到 hmi.log(看 %APPDATA%\QianWei\HMI\logs)
# vcxproj 也对齐改了(两个配置都 SubSystem=Windows)

qt_add_executable(hmi ${SOURCES} ${HEADERS} ${FORMS} ${RESOURCES})
# 阶段 A:Debug + Release 都用 Windows 子系统
# 启动没黑框,用户没法通过关黑框误杀程序;qDebug 输出到 hmi.log(看 %APPDATA%\QianWei\HMI\logs)
# vcxproj 也对齐改了(两个配置都 SubSystem=Windows)
set_target_properties(hmi PROPERTIES WIN32_EXECUTABLE TRUE)

target_include_directories(hmi PRIVATE src)
# ★ D.2(2026-09-20):QCustomPlot 头文件路径(LGPL v2.1)
target_include_directories(hmi PRIVATE 3rdparty/qcustomplot)
# ★ 阶段 C · C.2 Snap7 集成占位
#   - DLL 到位后启用 3rdparty/snap7/include/ 头文件搜索
#   - 当前骨架阶段用 QLibrary 动态加载,不静态链 snap7.lib(LGPL 合规)
#   - include 路径暂不添加,等 C.2.2 真加载 Snap7 函数指针时再开
# target_include_directories(hmi PRIVATE 3rdparty/snap7/include)  # C.2.2 启用

target_link_libraries(hmi PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    Qt6::PrintSupport  # ★ D.2:QCustomPlot 需要 QPrinter(虽然我们不打印,但 QCP 头文件 include)
    Qt6::SerialBus
    Qt6::Sql        # ★ D.1(2026-09-18):SQLite 历史库
)

# 中文字符串源码用 UTF-8 编码,需告诉 MSVC
target_compile_options(hmi PRIVATE /utf-8)