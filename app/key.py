#!/usr/bin/env python3
import subprocess
import os

def create_keystore():
    keystore_name = "release.keystore"
    
    if os.path.exists(keystore_name):
        print(f"⚠️ 发现已存在 {keystore_name}，无需重复生成。")
        return

    print(f"🔨 正在生成跨平台统一签名文件: {keystore_name}...")
    
    # 使用 JDK 自带的 keytool 工具生成标准的 jks/keystore 签名文件
    cmd = [
        "keytool", "-genkeypair",
        "-alias", "androiddebugkey",
        "-keyalg", "RSA",
        "-keysize", "2048",
        "-validity", "10000",
        "-keystore", keystore_name,
        "-storepass", "android",
        "-keypass", "android",
        "-dname", "CN=Android Debug, O=Android, C=US"
    ]
    
    try:
        subprocess.run(cmd, check=True)
        print(f"✅ 成功生成签名文件: {os.path.abspath(keystore_name)}")
    except FileNotFoundError:
        print("❌ 错误: 未找到 'keytool' 命令。请确保你的电脑或 Termux 环境中已正确安装 JDK (Java Development Kit)。")
    except subprocess.CalledProcessError as e:
        print(f"❌ 生成签名文件失败: {e}")

if __name__ == "__main__":
    create_keystore()
