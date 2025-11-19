# Qt UDP Monitor - 纯UDP架构监控系统

## 项目简介

这是一个基于Qt 6的纯UDP架构监控系统,用于实时监控和管理多个客户端。

**核心特性:**
- ✅ 纯UDP通信架构(无REST API依赖)
- ✅ 实时视频流接收(JPEG-over-UDP)
- ✅ 客户端心跳监测
- ✅ SQLite数据库存储
- ✅ 敏感词监控告警
- ✅ 截图管理
- ✅ 多客户端分组管理

## 技术栈

- **框架**: Qt 6.10.0
- **编译器**: MinGW 13.1.0
- **构建系统**: CMake + Ninja
- **数据库**: SQLite
- **编码**: TurboJPEG

## 架构说明

### 纯UDP通信架构

```
客户端 StreamClient (UDP发送)
    ↓ UDP 10000 (心跳+命令)
    ↓ UDP 5004 (视频流 JPEG)
    ↓
DesktopConsole (UDP接收)
    ↓
SQLite 数据库
```

### 核心模块

- **console/** - 主控制台GUI程序
- **libs/core/** - 核心配置和工具类
- **libs/network/** - UDP网络通信封装
- **config/** - 配置文件

## 编译说明

### 环境要求

- Qt 6.10.0 或更高版本
- CMake 3.20+
- Ninja 构建工具
- MinGW 13.1.0 编译器

### 编译步骤

```powershell
# 1. 配置项目
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.10.0/mingw_64"

# 2. 编译
cmake --build build --target console_app

# 3. 运行
.\build\console\console_app.exe
```

### 快速编译脚本

```powershell
# Windows PowerShell
.\build.ps1
```

## 数据库结构

主要表:
- **clients** - 客户端信息
- **activity_logs** - 活动日志
- **screenshots** - 截图记录
- **alerts** - 告警信息
- **app_usage** - 应用使用统计
- **sensitive_words** - 敏感词库

## UDP协议说明

### 控制端口: 10000

**心跳包格式:**
```json
{
  "type": "heartbeat",
  "client_id": "xxx",
  "hostname": "xxx",
  "ip_address": "xxx.xxx.xxx.xxx",
  "os_info": "Windows 11",
  "username": "xxx",
  "timestamp": "2025-11-19T09:00:00"
}
```

### 视频端口: 5004

**JP01协议 (JPEG-over-UDP):**
```
Header: "JP01"
Timestamp: 8 bytes
SSRC: 4 bytes
Sequence: 4 bytes
JPEG Data: Variable
```

## 配置文件

**config/app.json:**
```json
{
  "control_port": 10000,
  "video_port": 5004,
  "database_path": "./data/monitor.db",
  "screenshot_dir": "./screenshots",
  "log_dir": "./logs"
}
```

## 已知问题

- ⚠️ 纯UDP模式下无法主动向客户端发送命令(暂停/恢复等功能待实现)
- ⚠️ 窗口截图应用配置功能暂未实现

## Bug修复记录

### v1.0.0 (2025-11-19)

1. ✅ 修复客户端详情对话框REST API调用,改为纯数据库查询
2. ✅ 添加Tile双击全屏功能
3. ✅ 增强Tile状态栏显示(UDP端口 + SSRC + 帧号)

## 开发团队

- 架构设计: AI Agent
- 代码实现: AI Agent + 用户

## 许可证

MIT License

## 联系方式

有问题请提交Issue或联系开发者。
