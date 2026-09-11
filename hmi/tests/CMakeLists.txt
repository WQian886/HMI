# 阶段 A · A5 单元测试
# 用 Qt Test,跟主程序共用 core 源码

cmake_minimum_required(VERSION 3.21)
project(hmi_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test)
enable_testing()

# 主程序源码目录(用来 include 头文件)
set(HMI_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
include_directories(${HMI_SRC_DIR})

# 测试要链接的核心源文件
set(HMI_CORE_SOURCES
    ${HMI_SRC_DIR}/Variable.cpp
    ${HMI_SRC_DIR}/VariableManager.cpp
    ${HMI_SRC_DIR}/JsonSerializer.cpp
)

function(add_hmi_test test_name)
    add_executable(${test_name}
        ${test_name}.cpp
        ${HMI_CORE_SOURCES}
    )
    target_link_libraries(${test_name} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Test
    )
    target_compile_options(${test_name} PRIVATE /utf-8)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_hmi_test(tst_Variable)
add_hmi_test(tst_VariableManager)
add_hmi_test(tst_Serializer)

option(HMI_ENABLE_COVERAGE "Enable coverage build" OFF)