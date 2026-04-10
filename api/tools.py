import requests
import logging

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

# 配置信息
BASE_URL = "http://127.0.0.1:8000"  # 后端服务器地址
TIMEOUT = 30  # 超时时间

# 使用 Session 优化性能
http_session = requests.Session()

def retrieve_tool(query: str) -> str:
    """
    调用后端的检索工具接口，获取相关信息。
    """
    try:
        response = http_session.get(f"{BASE_URL}/retrieve", params={"query": query}, timeout=TIMEOUT)
        
        try:
            data = response.json()
        except ValueError:
            response.raise_for_status()
            return ""

        if response.ok:
            retrieved_docs = data.get("retrieved", [])
            if isinstance(retrieved_docs, list):
                return "\n".join(retrieved_docs)
            return str(retrieved_docs)
        else:
            err = data.get("error") if isinstance(data, dict) else str(data)
            logger.error(f"retrieve endpoint error: {err}")
            return f"Retrieve error: {err}"
    except requests.RequestException as e:
        logger.error(f"调用检索工具接口失败: {e}")
        return "检索工具调用失败，请稍后再试。"

def llm_tool(query: str) -> str:
    """
    调用后端的语言模型工具接口，获取生成的回复。
    """
    try:
        payload = {"query": query}
        response = http_session.post(f"{BASE_URL}/ask_raw", params=payload, timeout=TIMEOUT)
        
        try:
            data = response.json()
        except ValueError:
            response.raise_for_status()
            return ""

        if response.ok:
            reply = data.get("answer", "")
            return reply
        else:
            err = data.get("error") if isinstance(data, dict) else str(data)
            logger.error(f"ask_raw returned error: {err}")
            return f"LLM error: {err}"
    except requests.RequestException as e:
        logger.error(f"调用语言模型工具接口失败: {e}")
        return "语言模型工具调用失败，请稍后再试。"