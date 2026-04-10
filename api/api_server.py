# -*- coding: utf-8 -*-
from fastapi import FastAPI, Query, HTTPException
from fastapi.responses import JSONResponse
import subprocess
import redis
import os
import traceback
import asyncio

app = FastAPI()

# --- 配置区 ---
BUILD_DIR = r"E:\C++\code\llm_rag_cpp\out\build\x64-debug" 
RAG_BIN = os.path.join(BUILD_DIR, "llm_rag_cpp.exe")

# DLL 路径配置
os.environ["PATH"] = r"E:\C++\vcpkg\installed\x64-windows\bin;" + os.environ.get("PATH", "")

# Redis 连接
try:
    r = redis.Redis(host="localhost", port=6379, db=0, decode_responses=True)
    r.ping()
    REDIS_AVAILABLE = True
except Exception:
    REDIS_AVAILABLE = False

# --- 通用执行函数 ---
def call_cpp_rag(args: list):
    """封装对 C++ 程序的调用"""
    proc = subprocess.run(
        [RAG_BIN] + args,
        capture_output=True,
        text=True,
        cwd=BUILD_DIR,
        env=os.environ,
        timeout=60
    )
    if proc.returncode != 0:
        raise Exception(f"C++ Error: {proc.stderr}")
    return proc.stdout.strip()

# --- 接口实现 ---

@app.get("/retrieve")
def retrieve(query: str):
    """
    供 tools.py 调用。返回纯文本列表以适配 Agent。
    """
    # 尝试从 Redis 缓存获取
    if REDIS_AVAILABLE:
        cached = r.get(f"ret:{query}")
        if cached: return {"retrieved": cached.split("\n"), "cached": True}

    try:
        # 调用 C++: llm_rag_cpp.exe retrieve "query"
        stdout = call_cpp_rag(["retrieve", query])
        
        # 简单切分文档块
        chunks = [c.strip() for c in stdout.split('\n') if c.strip()]
        
        if REDIS_AVAILABLE:
            r.set(f"ret:{query}", "\n".join(chunks), ex=600) # 缓存10分钟

        return {"retrieved": chunks, "cached": False}
    except Exception as e:
        return JSONResponse(status_code=500, content={"error": str(e)})

@app.post("/ask_raw")
def ask_raw(query: str):
    """
    供 tools.py 调用。直接获取 LLM 的原始回答。
    """
    if REDIS_AVAILABLE:
        cached = r.get(f"ask:{query}")
        if cached: return {"answer": cached, "cached": True}

    try:
        # 调用 C++: llm_rag_cpp.exe ask "query"
        answer = call_cpp_rag(["ask", query])
        
        if REDIS_AVAILABLE:
            r.set(f"ask:{query}", answer, ex=3600)

        return {"answer": answer, "cached": False}
    except Exception as e:
        return JSONResponse(status_code=500, content={"error": str(e)})

@app.get("/chat")
async def chat_endpoint(msg: str, session_id: str = "default"):
    """
    亮点：直接在 API Server 暴露 Agent 多轮对话入口
    用户直接访问此接口即可实现：记忆 + 检索 + 回答
    """
    # Import inside try so import-time errors are caught and returned
    try:
        from agent import agent_answer
        # agent_answer is async; await it so we return the actual reply string
        answer = await agent_answer(msg, session_id)
        return {"reply": answer}
    except Exception as e:
        # return traceback to help debug in development
        tb = traceback.format_exc()
        # log to stderr
        print("[ERROR] Exception in /chat:", tb)
        return JSONResponse(status_code=500, content={"error": str(e), "trace": tb})

@app.get("/eval")
async def run_eval():
    try:
        import eval
        # 强制重新加载模块，防止旧代码缓存
        import importlib
        importlib.reload(eval)
        
        await eval.run_evaluation()
        return {"status": "Evaluation Finished. Check console for logs."}
    except Exception as e:
        # 将错误栈打印出来
        import traceback
        error_msg = traceback.format_exc()
        print(error_msg) # 打印到黑窗口
        return JSONResponse(status_code=500, content={"error": str(e), "detail": error_msg})

# --- 其他辅助接口 ---
@app.get("/health")
def health():
    return {"status": "ok", "redis": REDIS_AVAILABLE, "bin": os.path.isfile(RAG_BIN)}

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)