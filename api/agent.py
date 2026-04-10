import asyncio
from tools import retrieve_tool, llm_tool
from memory import ChatMemory

# 初始化记忆（建议由 api_server 传入 session_id）
memory = ChatMemory(max_turns = 10)

SYSTEM_PROMPT = """你是一个专业的智能助手。请遵循以下规则：
1. 优先根据【检索知识】回答问题。
2. 如果【检索知识】中没有相关信息，请告知用户，并结合自身知识给出建议。
3. 参考【对话历史】来理解指代词（如“它”、“那个人”）。
4. 回答要简洁、专业。"""


def build_final_prompt(query: str, context: str, history: str) -> str:
	return f"""{SYSTEM_PROMPT}
	【对话历史】
{history}
	【检索知识】
{context}
	【当前用户问题】
{query}
	请根据以上信息给出最终专业的回复。"""




async def agent_answer(query: str, session_id: str = "default") -> str:
	"""Asynchronous agent orchestration: optionally retrieve, call LLM, and update memory."""
	# 1) get history
	history = memory.get_messages(session_id)

	# 2) decide if retrieval is needed
	needs_retrieval = len(query) > 5 and not any(word in query for word in ["你好", "谢谢", "再见", "hi", "hello"])

	# 3) perform retrieval if needed (use thread for blocking HTTP)
	if needs_retrieval:
		# 实际开发中可用 asyncio.to_thread 包装 requests 调用
		context = await asyncio.to_thread(retrieve_tool, query)
	else:
		context = "无检索需求"

	# 3. 构建 Prompt
	prompt = build_final_prompt(query, context, history)

	# 4. 调用 LLM 
    # 亮点：可以增加重试机制

	answer = await asyncio.to_thread(llm_tool, prompt)

	# 6) update memory (store both user and assistant turns)
	memory.add_message(session_id, 'user', query)
	memory.add_message(session_id, 'assistant', answer)

	# return plain answer string for the API endpoint
	return answer
