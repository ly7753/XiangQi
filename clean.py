#!/usr/bin/env python3
# clean.py - 跨平台专业级构建缓存与临时文件清理工具

import os
import shutil
import fnmatch
import sys

# --- UI 色彩定义 ---
C_RESET = '\033[0m'
C_INFO = '\033[1;34m'
C_SUCCESS = '\033[1;32m'
C_WARN = '\033[1;33m'
C_ERROR = '\033[1;31m'

def info(msg):  print(f"{C_INFO}[INFO]{C_RESET} {msg}")
def success(msg): print(f"{C_SUCCESS}[SUCCESS]{C_RESET} {msg}")
def warn(msg):  print(f"{C_WARN}[WARN]{C_RESET} {msg}")
def error_msg(msg): print(f"  {C_ERROR}✘{C_RESET} {msg}")
def success_msg(msg): print(f"  {C_SUCCESS}✔{C_RESET} {msg}")
def skip_msg(msg): print(f"  {C_WARN}—{C_RESET} {msg}")

# --- 1. 固定清理目标配置 ---
CLEAN_TARGETS = [
    "app/build",
    "app/.cxx",
    "build",
    ".gradle",
]

# --- 2. 动态扫描模式配置 ---
BACKUP_PATTERNS = [
    "*.bak",
    "*~",
    "*.swp",
    "*.tmp",
    "*.log",
    ".DS_Store"
]

def cleanup():
    info("Starting deep clean for XiangQi...\n")
    
    deleted_targets = 0
    skipped_targets = 0
    backup_deleted = 0
    
    # ==========================================
    # 阶段 1: 清理固定的构建缓存和目录
    # ==========================================
    for target in CLEAN_TARGETS:
        if os.path.exists(target):
            try:
                if os.path.isdir(target):
                    shutil.rmtree(target) # 递归删除整个文件夹
                else:
                    os.remove(target)     # 删除单个文件
                success_msg(f"Removed: {target}")
                deleted_targets += 1
            except Exception as e:
                error_msg(f"Failed to remove: {target} ({e})")
        else:
            skip_msg(f"Skipped: {target} (Not found)")
            skipped_targets += 1
            
    print("")
    info("Scanning for backup and temporary files...")
    
    # ==========================================
    # 阶段 2: 深度扫描并肃清备份/临时文件
    # ==========================================
    for root, dirs, files in os.walk("."):
        # 避开 .git 目录以加快扫描速度
        if ".git" in dirs:
            dirs.remove(".git")
            
        for filename in files:
            # 检查文件名是否匹配任何预设的通配符
            if any(fnmatch.fnmatch(filename, pattern) for pattern in BACKUP_PATTERNS):
                filepath = os.path.join(root, filename)
                try:
                    os.remove(filepath)
                    backup_deleted += 1
                except Exception:
                    # 对于删不掉的临时文件静默跳过即可
                    pass
    
    if backup_deleted > 0:
        success_msg(f"Purged {backup_deleted} backup/temp file(s)")
    else:
        skip_msg("No backup files found. Project is already clean.")
        
    print("")
    success(f"Cleanup complete! (Dirs/Files: {deleted_targets}, Backups Purged: {backup_deleted}, Skipped: {skipped_targets})")

if __name__ == "__main__":
    try:
        cleanup()
    except KeyboardInterrupt:
        # 捕获 Ctrl+C 中断信号，优雅退出
        print("\n")
        warn("Cleanup aborted by user.")
        sys.exit(1)
