//! typofs —— Typora 的 S3 兼容图床客户端（Rust 版）
//!
//! 逻辑：读 config.ini、按年建桶、
//! 逐张顺序上传、对象名=前10字-5位随机字母+扩展名，输出 "Upload Success:" + 每个 URL。
//!
//! 构建: cargo build --release → target/release/typofs

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process;

use chrono::Local;
use rand::Rng;
use s3::BucketConfiguration;
use s3::bucket::Bucket;
use s3::creds::Credentials;
use s3::region::Region;

#[derive(Default)]
struct Config {
    endpoint: String,
    username: String,
    password: String,
    buckets: String,
    pre_url: String,
}

impl Config {
    fn load() -> Result<Self, String> {
        let path = find_config()?;
        let text = fs::read_to_string(&path)
            .map_err(|e| format!("读取 {} 失败: {}", path.display(), e))?;
        let mut c = Config::default();
        for line in text.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            if let Some((k, v)) = line.split_once('=') {
                let v = v.trim();
                match k.trim() {
                    "endpoint" => c.endpoint = v.to_string(),
                    "username" => c.username = v.to_string(),
                    "password" => c.password = v.to_string(),
                    "buckets" => c.buckets = v.to_string(),
                    "preUrl" | "preurl" => c.pre_url = v.to_string(),
                    _ => {}
                }
            }
        }
        Ok(c)
    }
}

fn find_config() -> Result<PathBuf, String> {
    if let Ok(exe) = env::current_exe() {
        if let Some(dir) = exe.parent() {
            let p = dir.join("config.ini");
            if p.exists() {
                return Ok(p);
            }
        }
    }
    let p = PathBuf::from("config.ini");
    if p.exists() {
        return Ok(p);
    }
    Err("未找到 config.ini，请把它放在二进制同目录".to_string())
}

/// 对象名：原文件名(去扩展名)前10字符 + "-" + 5位随机字母 + 原扩展名
fn build_file_name(file: &str) -> String {
    let path = Path::new(file);
    let stem: String = path
        .file_stem()
        .map(|s| s.to_string_lossy().to_string())
        .unwrap_or_default();
    let ext: String = path
        .extension()
        .map(|e| format!(".{}", e.to_string_lossy()))
        .unwrap_or_default();
    let trunc: String = stem.chars().take(10).collect();

    const CHARSET: &[u8] = b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    let mut rng = rand::thread_rng();
    let rand_str: String = (0..5)
        .map(|_| CHARSET[rng.gen_range(0..CHARSET.len())] as char)
        .collect();

    format!("{}-{}{}", trunc, rand_str, ext)
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let files: Vec<String> = env::args().skip(1).collect();
    if files.is_empty() {
        eprintln!("请传入图片路径");
        process::exit(1);
    }

    let cfg = Config::load()?;
    if cfg.endpoint.trim().is_empty() {
        eprintln!("endpoint 不能为空");
        process::exit(1);
    }
    // preUrl 可选：不填则用 endpoint（无 nginx/CDN 时通常相同）
    let base = if cfg.pre_url.trim().is_empty() { cfg.endpoint.trim().to_string() } else { cfg.pre_url.trim().to_string() };

    // 连接 MinIO（path-style），桶名按年
    let region = Region::Custom {
        region: "us-east-1".to_string(),
        endpoint: cfg.endpoint.clone(),
    };
    let creds = Credentials::new(Some(&cfg.username), Some(&cfg.password), None, None, None)?;
    let bucket_name = format!("{}-{}", cfg.buckets, Local::now().format("%Y"));

    // 建桶（桶已存在时 create 会报错，这里忽略之，等价于 MinIO 的 MakeBucket 幂等）
    let _ = Bucket::create(&bucket_name, region.clone(), creds.clone(), BucketConfiguration::default()).await;
    let bucket = Bucket::new(&bucket_name, region, creds)?.with_path_style();

    println!("Upload Success:");
    let mut failed = false;
    // 逐张顺序上传
    for f in &files {
        if !Path::new(f).exists() {
            eprintln!("文件不存在: {}", f);
            failed = true;
            continue;
        }
        let obj = build_file_name(f);
        let url = format!("{}/{}/{}", base, bucket_name, obj);
        match fs::read(f) {
            Ok(data) => match bucket.put_object(&obj, &data).await {
                Ok(_) => println!("{}", url),
                Err(e) => {
                    eprintln!("上传失败: {} ({:?})", obj, e);
                    failed = true;
                }
            },
            Err(e) => {
                eprintln!("读取失败: {} ({})", f, e);
                failed = true;
            }
        }
    }

    if failed {
        process::exit(1);
    }
    Ok(())
}
