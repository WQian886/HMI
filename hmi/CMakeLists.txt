cmake_minimum_required(VERSION 3.21)
project(hmi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_PREFIX_PATH "D:/Qt/6.8.3/msvc2022_64")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Widgets SerialBus)

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
file(GLOB EXTRA_CPP CONFIGURE_DEPENDS
    "*.cpp"
)
file(GLOB EXTRA_H CONFIGURE_DEPENDS
    "*.h"
)
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
qt_add_executable(hmi ${SOURCES} ${HEADERS} ${FORMS} ${RESOURCES})  # ★ 不写 WIN32 —— 保持 CONSOLE 子系统,与 hmi.vcxproj 一致,黑框 + qDebug 终端输出

target_link_libraries(hmi PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    Qt6::SerialBus
)

# 中文字符串源码用 UTF-8 编码,需告诉 MSVC
target_compile_options(hmi PRIVATE /utf-8)