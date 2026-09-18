# libplctag LGPL 集成占位目录

> 阶段 C · C.3 EtherNet/IP 协议适配器(2026-09-18 起)
> 维护人:Wqian + AI 助手

## 状态

**当前**: 目录骨架已建,等待 libplctag 动态库到位
**下一步**: 从 https://github.com/libplctag/libplctag/releases 下载 LGPL-2.1 版,放入下表对应位置

## 待放入的文件

| 子目录 | 放什么 | 来源 | 备注 |
|---|---|---|---|
| `include/` | `libplctag.h` | libplctag-2.x/lib/libplctag.h | C++ 头文件 |
| `lib/x64/` | `libplctag.lib` | libplctag-2.x/build/Release/libplctag.lib | MSVC 导入库 |
| `bin/x64/` | `libplctag.dll` | libplctag-2.x/build/Release/libplctag.dll | 运行时 DLL(可替换 → LGPL 合规) |

## LGPL-2.1 合规要点(规划 C.3 + C.6b)

1. **动态链接**:`libplctag.dll` 通过 `QLibrary` 运行时加载,主程序不静态链
2. **不修改源码**:用官方编译版本,不动
3. **用户可替换**:发布含 README 说明用户可自行替换 `libplctag.dll`
4. **版权声明**:`docs/阶段C.6b_LGPL合规清单.md`(C.6b 写) 含 libplctag 版权 + LGPL-2.1 全文链接
5. **不二次分发源码**:hmi.exe 闭源分发,libplctag 源码按 LGPL 可获取但不打包

## LibPlcTag API 关键函数(C.3.1 骨架预定义)

| 函数 | 类型 | 用途 |
|---|---|---|
| `plc_tag_create(attr, timeout, name)` | `int(*)(const char*, int, const char*)` | 创建 CIP tag handle |
| `plc_tag_destroy(tag)` | `void(*)(int)` | 销毁 handle |
| `plc_tag_read(tag, timeout)` | `int(*)(int, int)` | **异步**读启动 |
| `plc_tag_write(tag, timeout)` | `int(*)(int, int)` | **异步**写启动 |
| `plc_tag_status(tag)` | `int(*)(int)` | 轮询异步操作状态 |
| `plc_tag_get_int_attribute(tag, name, dst)` | `int(*)(int, const char*, int*)` | 读 tag 属性 |
| `plc_tag_set_int_attribute(tag, name, val)` | `int(*)(int, const char*, int)` | 设 tag 属性 |
| `plc_tag_abort(tag)` | `void(*)(int)` | 取消异步操作 |

## 不放什么

- ❌ libplctag 源码(`.c`/`.cpp`/`.h` 之外的)
- ❌ libplctag debug 版 `.pdb`
- ❌ 任何修改过的 libplctag.dll(用官方版)

## 历史

- 2026-09-18 目录骨架建立,等待下载
