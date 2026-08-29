# 服务端：RustFS 图床存储

独立于其它部件的图床对象存储（S3 兼容），供客户端的 `typofs` 上传图片、并对外提供**永久公开**的图片 URL。

## 组件

| 服务 | 端口 | 说明 |
|------|------|------|
| `tc_rustfs` | `9100`（S3 API）、`9101`（控制台） | rustfs/rustfs，独立数据卷 `tc_rustfs_data` |

默认凭据：`rustfsadmin / rustfsadmin`（**弱**，生产环境请改强随机）。

## 启动

```bash
docker compose up -d
```

## 建桶 + 设为公开读（让 URL 永久可访问）

用官方 `mc` 客户端（国内网络可用中国镜像下载）：

```bash
# 1) 下载 mc（国内镜像）
curl -fsSL --max-time 60 -o /tmp/mc https://dl.minio.org.cn/client/mc/release/linux-amd64/mc
chmod +x /tmp/mc

# 2) 设 alias
/tmp/mc alias set tc http://10.0.0.45:9100 rustfsadmin rustfsadmin

# 3) 建桶 + 公开读（桶名 = config.ini 的 buckets + "-" + 年份）
/tmp/mc mb --ignore-existing tc/tuchuang-2026
/tmp/mc anonymous set download tc/tuchuang-2026
```

> 桶名随年份变化（`tuchuang-<年份>`），每年首次上传后需再执行一次上面的 `mb` + `anonymous set download`（或用固定桶名）。

## URL 有效期

- **公开读桶直连 URL**（`http://<host>:9100/<桶>/<对象>`）：**永久有效**。
- 预签名/分享链接（`mc share link`、私有桶）：会过期（默认约 7 天），图床不用，所以不要走私有桶 + 预签名。

## 安全

- `anonymous set download` = 匿名可读（看对象、列桶），但**匿名不能写**（上传需密钥）。
- ⚠️ 默认凭据 `rustfsadmin` 是弱密码。若 `9100` 暴露到公网/不可信网络，**务必**：
  - 把 `server/docker-compose.yml` 里的 `RUSTFS_ACCESS_KEY`/`RUSTFS_SECRET_KEY` 改成强随机（`openssl rand -base64 24`），重建；
  - 或用防火墙只放行可信 IP/网段访问 `9100`。
- 若要禁止枚举对象，用只允许 `s3:GetObject` 的自定义桶策略。

## 目录结构

```
server/
└── docker-compose.yml   # tc_rustfs 独立图床存储
```
