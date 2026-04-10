<template>
  <div class="chat-wrapper">
    <div class="message-container">
      <el-scrollbar ref="scrollbarRef">
        <div class="message-content">
          <div 
            v-for="(msg, index) in messages" 
            :key="index" 
            :class="['msg-row', msg.role]"
          >
            <div class="avatar-wrapper">
              <el-avatar 
                :size="40"
                :src="msg.role === 'user' 
                  ? `https://api.dicebear.com/7.x/avataaars/svg?seed=${username || 'guest'}` 
                  : `https://api.dicebear.com/7.x/bottts/svg?seed=Lucky`" 
              />
            </div>

            <div class="msg-bubble">
              <div v-if="msg.loading" class="thinking-wrapper">
                <div class="dot-typing"></div>
                <span class="thinking-text">正在检索知识库...</span>
              </div>
              
              <div v-if="msg.content" class="content-text">{{ msg.content }}</div>
              
              <div v-if="msg.sources && msg.sources.length > 0" class="source-container">
                <div class="source-title"><el-icon><Collection /></el-icon> 知识库引用</div>
                <div v-for="(src, sIdx) in msg.sources" :key="sIdx" class="source-card">{{ src }}</div>
              </div>
              
              <div v-if="msg.latency" class="metrics-info">
                <span class="latency-tag">⚡ 检索耗时: {{ msg.latency.toFixed(2) }}ms</span>
              </div>
            </div>
          </div>
        </div>
      </el-scrollbar>
    </div>

    <div class="input-area">
      <div class="input-box-wrapper">
        <el-input 
          v-model="query" 
          placeholder="输入您的问题 (Enter 发送)..." 
          @keyup.enter.exact="onSend" 
          :disabled="loading"
          type="textarea"
          :autosize="{ minRows: 1, maxRows: 4 }"
        />
        <el-button 
          class="send-btn" 
          @click="onSend" 
          :loading="loading" 
          type="primary"
        >发送</el-button>
      </div>
      <p class="copyright">基于 C++ RAG Engine 提供技术支持</p>
    </div>
  </div>
</template>

<script setup>
import { ref, nextTick, reactive, onMounted, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { Collection } from '@element-plus/icons-vue'
import { askQuestionStream } from '../api/index'

const props = defineProps({
  username: String,
  sessions: { type: Array, default: () => [] }
})
const emit = defineEmits(['update-sessions'])

const route = useRoute()
const router = useRouter()
const query = ref('')
const loading = ref(false)
const scrollbarRef = ref(null)

// 欢迎语工厂函数
const getWelcomeMessage = () => ({
  role: 'assistant',
  content: `你好 ${props.username || '陈贤良'}！我是 RAG 助手，已连接到 C++ 向量检索引擎。请问有什么我可以帮您的？`
})

// 初始状态即包含欢迎语
const messages = ref([getWelcomeMessage()])

const scrollToBottom = () => {
  if (scrollbarRef.value) {
    const wrap = scrollbarRef.value.$el.querySelector('.el-scrollbar__wrap')
    if (wrap) scrollbarRef.value.setScrollTop(wrap.scrollHeight)
  }
}

// 同步消息逻辑
const syncMessages = () => {
  const sessionId = route.params.id
  if (sessionId && props.sessions.length > 0) {
    const target = props.sessions.find(s => String(s.id) === String(sessionId))
    if (target) {
      messages.value = [...target.messages]
      nextTick(scrollToBottom)
      return
    }
  }
  // 如果是首页 (/) 或没找到会话，显示欢迎语
  messages.value = [getWelcomeMessage()]
  nextTick(scrollToBottom)
}

// 监听路由变化
watch(() => route.params.id, syncMessages, { immediate: true })
// 监听 sessions 变化（处理刷新后数据异步加载的情况）
watch(() => props.sessions, (newVal) => {
  if (newVal && newVal.length > 0) syncMessages()
}, { deep: true })

const onSend = async () => {
  if (!query.value.trim() || loading.value) return
  const userQuery = query.value
  let currentId = route.params.id
  
  if (!currentId) {
    // --- 新建会话逻辑 ---
    const newId = Date.now().toString()
    const assistantMsg = reactive({
      role: 'assistant',
      content: '',
      loading: true,
      sources: [],
      latency: 0
    })
    const newSession = {
      id: newId,
      title: userQuery.substring(0, 12),
      // 重要：新建会话时，消息数组第一项放欢迎语，第二项放用户问题
      messages: [
        getWelcomeMessage(),
        { role: 'user', content: userQuery },
        assistantMsg
      ]
    }
    
    // 发送给父组件保存到 localStorage
    emit('update-sessions', [newSession, ...props.sessions])
    
    // 跳转到新路由
    router.push(`/chat/${newId}`)

    // 先让本组件立刻显示用户和助手占位，避免“第一次点没反应”
    messages.value = [...newSession.messages]
    query.value = ''
    loading.value = true
    nextTick(scrollToBottom)

    try {
      const response = await askQuestionStream(userQuery)
      const reader = response.body.getReader()
      const decoder = new TextDecoder()
      assistantMsg.loading = false

      let buffer = ''
      while (true) {
        const { done, value } = await reader.read()
        if (done) break
        buffer += decoder.decode(value, { stream: true })

        const parts = buffer.split('\n\n')
        buffer = parts.pop()

        for (const part of parts) {
          if (!part.startsWith('data: ')) continue
          const payload = part.substring(6)
          if (payload === '[DONE]') break
          try {
            const obj = JSON.parse(payload)
            if (obj.type === 'metadata') {
              assistantMsg.latency = obj.latency
              assistantMsg.sources = obj.sources || []
            } else if (obj.content) {
              assistantMsg.content += obj.content
            }
          } catch (e) {
            if (payload !== '[DONE]') assistantMsg.content += payload
          }
        }
        nextTick(scrollToBottom)
      }

      const updated = props.sessions.map(s => {
        if (String(s.id) === String(newId)) {
          return { ...s, messages: [...messages.value] }
        }
        return s
      })
      emit('update-sessions', updated)
    } catch (err) {
      console.error('Stream Error:', err)
      assistantMsg.content = '服务连接失败，请检查后端 API。'
      assistantMsg.loading = false
    } finally {
      loading.value = false
    }
    return
  }

  // --- 现有会话逻辑 ---
  messages.value.push({ role: 'user', content: userQuery })
  
  const assistantMsg = reactive({ 
    role: 'assistant', 
    content: '', 
    loading: true, 
    sources: [], 
    latency: 0 
  })
  messages.value.push(assistantMsg)
  
  loading.value = true
  query.value = ''
  nextTick(scrollToBottom)

  try {
    const response = await askQuestionStream(userQuery)
    const reader = response.body.getReader()
    const decoder = new TextDecoder()
    assistantMsg.loading = false

    let buffer = ''
    while (true) {
      const { done, value } = await reader.read()
      if (done) break
      buffer += decoder.decode(value, { stream: true })
      
      const parts = buffer.split('\n\n')
      buffer = parts.pop()
      
      for (const part of parts) {
        if (!part.startsWith('data: ')) continue
        const payload = part.substring(6)
        if (payload === '[DONE]') break
        try {
          const obj = JSON.parse(payload)
          if (obj.type === 'metadata') {
            assistantMsg.latency = obj.latency
            assistantMsg.sources = obj.sources || []
          } else if (obj.content) {
            assistantMsg.content += obj.content
          }
        } catch (e) {
          // 处理非 JSON 格式的流数据
          if (payload !== '[DONE]') assistantMsg.content += payload
        }
      }
      nextTick(scrollToBottom)
    }
    
    // 请求结束后，将最新消息数组同步回父组件以持久化
    const updated = props.sessions.map(s => {
      if (String(s.id) === String(currentId)) {
        return { ...s, messages: [...messages.value] }
      }
      return s
    })
    emit('update-sessions', updated)

  } catch (err) {
    console.error("Stream Error:", err)
    assistantMsg.content = '服务连接失败，请检查后端 API。'
    assistantMsg.loading = false
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.chat-wrapper { 
  display: flex; 
  flex-direction: column; 
  height: 100%; 
  width: 100%; 
  background: #fff; 
  overflow: hidden; 
}

.message-container { 
  flex: 1; 
  overflow: hidden; 
  position: relative; 
}

.message-content { 
  padding: 40px 0; 
  max-width: 900px; 
  margin: 0 auto; 
}

.msg-row { 
  display: flex; 
  padding: 15px 20px; 
  gap: 16px; 
  margin-bottom: 10px; 
}

.msg-row.user { 
  flex-direction: row-reverse; 
}

.msg-bubble { 
  max-width: 75%; 
  padding: 12px 16px; 
  border-radius: 12px; 
  font-size: 15px; 
  line-height: 1.6;
  white-space: pre-wrap;
  word-break: break-word;
}

.user .msg-bubble { 
  background-color: #f4f4f5; 
  color: #303133;
}

.assistant .msg-bubble { 
  background-color: #fff; 
  border: 1px solid #f0f0f0; 
  color: #303133;
  box-shadow: 0 2px 4px rgba(0,0,0,0.02);
}

.source-container { 
  margin-top: 15px; 
  background: #f8f9fb; 
  padding: 12px; 
  border-radius: 8px; 
  border: 1px solid #ebedf0; 
}

.source-title { 
  font-size: 12px; 
  color: #909399; 
  margin-bottom: 8px; 
  display: flex;
  align-items: center;
  gap: 5px;
}

.source-card { 
  font-size: 13px; 
  background: #fff; 
  padding: 8px 12px; 
  border-left: 3px solid #409eff; 
  margin-bottom: 6px; 
}

.input-area { 
  padding: 20px; 
  background: #fff; 
  border-top: 1px solid #f2f6fc; 
}

.input-box-wrapper { 
  max-width: 900px; 
  margin: 0 auto; 
  display: flex; 
  gap: 12px; 
  align-items: flex-end; 
  border: 1px solid #dcdfe6; 
  border-radius: 12px; 
  padding: 8px 12px; 
  transition: border-color 0.3s;
}

.input-box-wrapper:focus-within {
  border-color: #409eff;
}

.copyright { 
  text-align: center; 
  font-size: 12px; 
  color: #c0c4cc; 
  margin-top: 10px; 
}

.thinking-wrapper { 
  display: flex; 
  align-items: center; 
  gap: 10px; 
  color: #909399; 
}

.dot-typing { 
  width: 6px; 
  height: 6px; 
  border-radius: 50%; 
  background: #909399; 
  box-shadow: 12px 0 #909399, 24px 0 #909399; 
  animation: dotTyping 1.5s infinite linear; 
  margin-right: 30px; 
}

@keyframes dotTyping { 
  0%, 100% { opacity: 0.3; } 
  50% { opacity: 1; } 
}

.metrics-info { 
  margin-top: 8px; 
  font-size: 11px; 
  color: #c0c4cc; 
  text-align: right; 
}

:deep(.el-textarea__inner) {
  border: none !important;
  box-shadow: none !important;
  padding: 8px 0 !important;
}
</style>