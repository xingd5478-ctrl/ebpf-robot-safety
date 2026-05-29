#!/usr/bin/env python3
"""
一键同步脚本：同时更新论文的 LaTeX 和 DOCX 版本。

用法：
    python update_paper.py            # 从 LaTeX 重新生成 DOCX
    python update_paper.py --pdf      # 同时编译 LaTeX PDF（需要 xelatex）

说明：
    - LaTeX 主文件:      论文/paper/journal_paper.tex
    - DOCX 输出:         C:/Users/xing2/Desktop/journal_paper_final.docx
    - PDF 输出:          论文/paper/journal_paper.pdf
    - 架构图:            论文/paper/figures/system_architecture.pdf

修改论文时：
    1. 编辑 journal_paper.tex（LaTeX 源文件）
    2. 运行 python update_paper.py（自动同步 LaTeX → DOCX）
    3. 桌面上的 journal_paper_final.docx 自动更新
"""

import os
import sys
import subprocess

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
LATEX_FILE = os.path.join(PROJECT_ROOT, "paper", "journal_paper.tex")
DOCX_OUTPUT = os.path.join(os.path.expanduser("~"), "Desktop", "journal_paper_final.docx")
GENERATOR = os.path.join(PROJECT_ROOT, "generate_paper.py")

def run_docx():
    """从 LaTeX 内容生成 DOCX"""
    print("[1/2] 生成 DOCX...")
    result = subprocess.run(
        [sys.executable, GENERATOR],
        capture_output=True, text=True, cwd=PROJECT_ROOT
    )
    if result.returncode != 0:
        print(f"  ERROR: {result.stderr}")
        return False
    print(f"  OK → {DOCX_OUTPUT}")
    return True

def run_pdf():
    """使用 xelatex 编译 PDF"""
    print("[2/2] 编译 LaTeX PDF...")
    paper_dir = os.path.dirname(LATEX_FILE)
    tex_name = os.path.basename(LATEX_FILE)

    for _ in range(2):  # 两次编译解决交叉引用
        result = subprocess.run(
            ["xelatex", "-interaction=nonstopmode", tex_name],
            capture_output=True, text=True, cwd=paper_dir
        )
    if result.returncode != 0:
        # 检查是否只是 warning
        if "Fatal error" in result.stdout or "Error:" in result.stdout:
            print(f"  WARNING: LaTeX 编译可能有错误，请检查 .log 文件")
        else:
            print("  OK (有 warning 但编译完成)")
    else:
        print("  OK")

    pdf_path = LATEX_FILE.replace(".tex", ".pdf")
    if os.path.exists(pdf_path):
        size_kb = os.path.getsize(pdf_path) / 1024
        print(f"  PDF: {pdf_path} ({size_kb:.1f} KB)")
    return True

if __name__ == "__main__":
    if not os.path.exists(LATEX_FILE):
        print(f"ERROR: 找不到 LaTeX 文件: {LATEX_FILE}")
        sys.exit(1)

    ok = run_docx()
    if not ok:
        sys.exit(1)

    if "--pdf" in sys.argv:
        run_pdf()

    print("\n完成。桌面上的 journal_paper_final.docx 已是最新版本。")
