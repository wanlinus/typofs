# typofs (C)

C 实现的 Typora **S3 兼容**图床客户端（**仅 Windows / MSVC，只用 Windows SDK**）。读 config.ini、按年建桶、逐张顺序上传、对象名 `前10字-5位随机+扩展名`、输出 `Upload Success:` + 每个 URL。

存储不限 MinIO/RustFS，只要是 S3 兼容的对象存储即可（配好 endpoint/账号/preUrl）。

## 依赖（无需 vcpkg）

只用 Windows 原生 API，**不需要第三方库**：

- HTTP/HTTPS：**WinHTTP**
- 签名（SHA256/HMAC-SHA256）：**CNG / BCrypt**

只需 **Windows SDK**（装了 Visual Studio 的 C++ 桌面负载就有），不需要 vcpkg、curl、openssl。

## 构建

### 方式一：一键脚本（推荐）

在 **"x64 Native Tools Command Prompt for VS 2022"** 里：

```bat
build.bat
```

产物 `.\typofs.exe`。

### 方式二：CMake + MSVC

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
:: 产物 build\Release\typofs.exe
```

### 方式三：直接 cl 编译

```bat
cl /utf-8 /std:c11 /O2 main.c /Fe:typofs.exe winhttp.lib bcrypt.lib
```

产物 `.\typofs.exe`。

## 配置

在 exe 同目录放 `config.ini`（参考仓库根 `config.example.ini`）：

```ini
endpoint = http://10.0.0.45:9100
username = <access key>
password = <secret key>
buckets = tuchuang
# preUrl = http://10.0.0.45:9100   # 可选，缺省=endpoint
```

## 使用

```bat
typofs.exe C:\path\img1.png C:\path\img2.jpg
```

Typora「图像 → 上传服务 → 自定义命令」填该 exe 绝对路径，点「验证图片上传」。

> ⚠️ 若放在含空格的目录（如 `C:\Program Files\typofs\`），「自定义命令」里的路径要用英文引号包起来：`"C:\Program Files\typofs\typofs.exe"`。

## 说明

- 默认走 **http**（适配 MinIO/RustFS），`endpoint` 填 `host:port` 或带 `http://`/`https://` 前缀；`https://` 前缀才启用 HTTPS。缺省端口 http=80 / https=443，非默认端口签名 Host 会带上端口。
- S3 桶默认私有，若走静态 `preUrl` URL，需将 bucket 设为公开读（`mc anonymous set download`）。
- 上传用 `UNSIGNED-PAYLOAD` 简化签名，无需读文件算 SHA256。
- 任一文件失败时退出码非 0（成功项输出到 stdout、失败项到 stderr）。
- C 版内置 `SetConsoleOutputCP(65001)` 处理中文输出；若 Typora 里乱码，命令前加 `@chcp 65001 >nul & cmd /d/s/c`。
