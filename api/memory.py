# 实现多轮对话的记忆功能，保存用户的输入和模型的回复，以便在后续对话中使用这些信息。
class ChatMemory:
	def __init__(self, max_turns=10):
		# sessions: { session_id: [ {role, content}, ... ] }
		self.sessions = {}
		self.max_turns = max_turns

	def add_message(self, session_id, role, content):
		"""
		role: 'user' or 'assistant'
		"""
		if session_id not in self.sessions:
			self.sessions[session_id] = []
		self.sessions[session_id].append({'role': role, 'content': content})

		# # 保持滑动窗口 (注意一轮对话包含 user 和 assistant 两条消息)
		if len(self.sessions[session_id]) > self.max_turns * 2:
			self.sessions[session_id] = self.sessions[session_id][-self.max_turns * 2:]

	def get_messages(self, session_id, system_prompt="You are an assistant for a RAG system"):
		"""
        返回符合 OpenAI/Anthropic 标准的 Messages 格式
		"""
		history = self.sessions.get(session_id, [])
		if not history:
			return system_prompt
		lines = [f"{m['role']}: {m['content']}" for m in history]
		return system_prompt + "\n" + "\n".join(lines)

	def clear(self, session_id):
		if session_id in self.sessions:
			del self.sessions[session_id]

