# typofs

> Typora 的 **S3 兼容图床客户端**（Rust 版 + C 版）。把本地图片上传到你自己的对象存储，并返回**永久公开**的图片 URL，让 Typora 正文里的图片长期有效、随处可访问。

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Rust](https://img.shields.io/badge/Rust-stable-000000.svg)](rust)
[![C](https://img.shields.io/badge/C-MSVC-00599C.svg)](c)
[![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)](#)

用 **Rust** 与 **C** 实现；存储不限 MinIO/RustFS，只要是 **S3 兼容**的对象存储即可（MinIO、RustFS、AWS S3、Ceph…）。

## 特性

- **两个实现**：Rust（用 `rust-s3` SDK）与 C（只用 **Windows SDK**：WinHTTP + CNG/BCrypt，零第三方依赖、无需 vcpkg/curl/openssl）。
- **S3 兼容**：对任意的 S3 兼容存储做上传（`PUT` + SigV4 签名）。
- **永久 URL**：桶设为公开读，返回的静态 URL 长期有效（不用会过期的预签名链接）。
- **命名友好**：桶 `tuchuang-<年份>`、对象 `文件名前10字-5位随机+扩展名`，可读又防撞。
- **按年分桶**：自动归档，无需额外状态管理。

## 架构

```
┌───────────── 你的 Windows 电脑 ─────────────┐
│  Typora (编辑器)                             │
│    │ 插入图片时把本地路径传给自定义命令        │
│    ▼                                         │
│  typofs (typofs.exe)                         │
│    │ 读 config.ini → S3 签名 PUT 上传         │
│    │ 输出 "Upload Success:" + 每个 URL        │
│    ▼ ⑤ Typora 取 stdout 最后 N 行作为链接      │
└──────────────┬──────────────────────────────┘
               │ HTTP (S3 API)
               ▼
┌───────────── 服务器 (自建) ─────────────────┐
│  S3 兼容对象存储 (server/docker-compose.yml) │
│    桶公开读                                  │
│    URL: http://<host>:9100/<桶>/<对象>  ← 永久 │
└────────────────────────────────────────────┘
```

> 无需 nginx 公共层：S3 端口直接对外服务对象；`preUrl` 可选，缺省等于 `endpoint`。

## 目录结构

```
typofs/
├── rust/            # Rust 客户端 (typofs.exe)
│   ├── Cargo.toml
│   └── src/main.rs
├── c/               # C 客户端 (typofs.exe) —— 仅 Windows SDK
│   ├── main.c
│   ├── CMakeLists.txt
│   ├── build.bat
│   └── Makefile
├── server/          # 服务端 S3 存储 (docker-compose)
│   ├── docker-compose.yml
│   └── README.md
├── config.example.ini
├── LICENSE
└── README.md
```

## 构建

### Rust

```bash
cd rust
cargo build --release
# 产物: target/release/typofs.exe
```

### C（仅 Windows / MSVC）

在任意终端（脚本会自动定位 VS 的 `vcvars64.bat`）：

```bat
cd c
build.bat
REM 或 CMake:
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物：`c\typofs.exe` 或 `build\Release\typofs.exe`。

## 配置 `config.ini`

放到 `typofs.exe` **同目录**（参考 `config.example.ini`）：

```ini
endpoint = http://10.0.0.45:9100     # S3 地址；默认 http，填 https://... 走 https
username = <access key>
password = <secret key>
buckets  = tuchuang                  # 桶前缀，实际成为 <buckets>-<年份>
# preUrl  = http://10.0.0.45:9100    # 可选；缺省=endpoint（无反代/CDN 时无需填）
```

> `endpoint`/`preUrl` 默认走 http；填 `https://...` 前缀则启用 HTTPS。
> `preUrl` 可选：图片若经反代/CDN 暴露再填，否则自动用 `endpoint`。

## 使用（Typora）

1. **偏好设置 → 图像 → 上传服务 → 自定义命令**，填 `typofs.exe` 绝对路径。
2. 点 **「验证图片上传选项」** → 显示 `Upload Success:` 和两个 URL 即成功。
3. 之后插入/粘贴图片会自动上传，正文替换为 `http://<host>:<port>/<桶>/<对象>`。

> ⚠️ **含空格的路径要加引号**：若把 `typofs.exe` 放在含空格的目录（如 `C:\Program Files\typofs\`），在「自定义命令」里必须用**英文引号**把路径包起来，否则命令行会把路径按空格拆开而报错。例如：
> ```
> "C:\Program Files\typofs\typofs.exe"
> ```

> Typora 从 stdout 取**最后 N 行**作为图片 URL，所以 `Upload Success:` 前缀不影响。

## 服务端部署（可选）

用 `server/docker-compose.yml` 起一个独立的 S3 兼容存储（默认 RustFS，S3 端口 `9100`），并把桶设为公开读：

```bash
docker compose up -d
mc alias set tc http://<host>:9100 <access> <secret>
mc mb --ignore-existing tc/tuchuang-2026
mc anonymous set download tc/tuchuang-2026
```

详见 [`server/README.md`](server/README.md)。

## 关于 URL 有效期

- **公开读桶直连 URL**（`http://.../桶/对象`）：**永久有效**，只要桶保持公开读。
- **预签名/分享链接**（私有桶）：**会过期**（默认约 7 天），本项目不用。

## 安全提示

- 桶用 `download`（公开读）策略：匿名**可读**（看对象、列桶），但**匿名不能写**（上传需密钥）。
- ⚠️ 若存储用默认弱凭据并暴露到公网，**务必改为强随机密钥**并只对可信网段开放 `9100`。
- 不想让人枚举对象，可用只允许 `s3:GetObject` 的桶策略。

## 排除（Troubleshooting）

- **只有 `Upload Success:` 无 URL**：上传失败，看 stderr（命令行加 `2>&1`）。多为 `config.ini` 账号/endpoint 错、或 region 用错（RustFS 用 `us-east-1`）。
- **URL 打不开**：桶是否公开读；已移除 nginx，直接用 S3 端口。
- **C 版输出乱码**：已内置 `SetConsoleOutputCP(65001)`。

## 许可证

[MIT](LICENSE) © [Wanli](https://github.com/wanlinus)。
