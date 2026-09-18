# Snap7 LGPL 集成占位目录

> 阶段 C · C.2 S7 协议适配器(2026-09-18 起)
> 维护人:Wqian + AI 助手

## 状态

**当前**: 目录骨架已建,等待 Snap7 动态库到位
**下一步**: 从 http://snap7.sourceforge.net/ 下载 LGPL 版,放入下表对应位置

## 待放入的文件

| 子目录 | 放什么 | 来源 | 备注 |
|---|---|---|---|
| `include/` | `snap7.h` | snap7-1.4.x/snap7.h | C 头文件 |
| `lib/x64/` | `snap7.lib` | snap7-1.4.x/build/x64/Release/snap7.lib | MSVC 导入库 |
| `bin/x64/` | `snap7.dll` | snap7-1.4.x/build/bin/x64/Release/snap7.dll | 运行时 DLL(可替换 → LGPL 合规) |

## LGPL 合规要点(规划 C.2 + C.6b)

1. **动态链接**:`snap7.dll` 通过 `QLibrary` 运行时加载,主程序不静态链 .lib
2. **不修改源码**:`snap7.dll` 用官方编译版本,不动
3. **用户可替换**:发布包含 README 说明用户可自行替换 `snap7.dll` 以升级/降级
4. **版权声明**:`docs/阶段C_LGPL合规.md`(C.6b 写) 含 Snap7 版权 + LGPL 全文链接
5. **不二次分发源码**:hmi.exe 闭源分发,Snap7 源码按 LGPL 可获取但不打包

## 不放什么

- ❌ Snap7 源码(`.c`/`.cpp`)
- ❌ Snap7 debug 版 `.pdb`
- ❌ 任何修改过的 snap7.dll(用官方版)

## 历史

- 2026-09-18 目录骨架建立,等待下载
