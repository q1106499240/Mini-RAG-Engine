<template>
  <div v-if="!isLoggedIn" class="login-wrapper">
    <div class="login-card">
      <div class="login-header">
        <el-icon :size="40" color="#409eff"><Cpu /></el-icon>
        <h1>C++ RAG Engine</h1>
        <p>高性能向量检索系统</p>
      </div>
      <el-radio-group v-model="authMode" size="small" style="margin-bottom: 14px; width: 100%;">
        <el-radio-button label="login">登录</el-radio-button>
        <el-radio-button label="register">注册</el-radio-button>
      </el-radio-group>
      <el-form label-position="top" @keyup.enter="authMode === 'login' ? handleLogin() : handleRegister()">
        <el-form-item label="账号">
          <el-input v-model="loginForm.username" placeholder="请输入用户名" />
        </el-form-item>
        <el-form-item label="邮箱" v-if="authMode === 'register'">
          <el-input v-model="loginForm.email" placeholder="请输入邮箱(注册时必填)" />
        </el-form-item>
        <el-form-item label="密码">
          <el-input v-model="loginForm.password" type="password" placeholder="请输入密码" show-password />
        </el-form-item>
        <el-button v-if="authMode === 'login'" type="primary" class="login-submit" @click="handleLogin" style="width: 100%;">登录</el-button>
        <el-button v-else type="primary" plain @click="handleRegister" style="width: 100%">注册并登录</el-button>
      </el-form>
    </div>
  </div>

  <div v-else class="rag-layout">
    <el-container class="full-height-container">
      <el-aside width="280px" class="system-sidebar">
        <div class="brand">
          <el-icon :size="24"><Cpu /></el-icon>
          <span>C++ RAG Engine</span>
        </div>

        <el-menu
          background-color="#001529"
          text-color="#fff"
          active-text-color="#1890ff"
          :default-active="route.path"
          router
          class="sidebar-menu"
        >
          <el-sub-menu index="chat-center">
            <template #title>
              <el-icon><ChatDotRound /></el-icon>
              <span>对话中心</span>
            </template>

            <el-menu-item index="/" @click="handleNewChat">
              <el-icon><Plus /></el-icon>
              <span>新建对话</span>
            </el-menu-item>

            <el-menu-item 
              v-for="session in sessions" 
              :key="session.id" 
              :index="'/chat/' + session.id"
              class="history-item"
            >
              <el-icon><Message /></el-icon>
              <template #title>
                <span class="session-title">{{ session.title }}</span>
                <el-icon class="delete-btn" @click.stop="handleDeleteSession(session.id)"><Close /></el-icon>
              </template>
            </el-menu-item>
          </el-sub-menu>

          <el-menu-item index="/eval" v-if="can('log:read')">
            <el-icon><DataLine /></el-icon>
            <span>评测仪表盘</span>
          </el-menu-item>
        </el-menu>

        <div class="sidebar-footer">
          <div class="user-info">
            <el-avatar :size="24" src="https://api.dicebear.com/7.x/bottts/svg?seed=admin" />
            <span class="user-name">{{ loginForm.username }}</span>
          </div>
          <div style="display: flex; gap: 8px;">
            <el-button v-if="can('user:manage')" type="warning" plain @click="openUserRoleDialog" size="small">用户权限</el-button>
            <el-button type="danger" plain @click="handleLogout" size="small">退出</el-button>
          </div>
        </div>
      </el-aside>

      <el-main class="main-content">
        <router-view v-slot="{ Component }">
          <keep-alive>
            <component
              :is="Component"
              :key="route.fullPath"
              :username="loginForm.username"
              :sessions="sessions"
              @update-sessions="syncSessions"
            />
          </keep-alive>
        </router-view>
      </el-main>
    </el-container>

    <el-dialog v-model="userRoleDialogVisible" title="用户权限管理" width="760px">
      <div style="margin-bottom: 10px; color: #666;">仅高级管理员可操作</div>
      <el-table :data="adminUsers" size="small" border>
        <el-table-column prop="id" label="ID" width="70" />
        <el-table-column prop="user_name" label="用户名" />
        <el-table-column prop="email" label="邮箱" />
        <el-table-column label="当前角色">
          <template #default="{ row }">
            <el-tag v-for="r in (row.role_codes || [])" :key="r" size="small" style="margin-right: 6px;">{{ r }}</el-tag>
            <span v-if="!row.role_codes || row.role_codes.length === 0">未分配</span>
          </template>
        </el-table-column>
        <el-table-column label="修改角色" width="220">
          <template #default="{ row }">
            <el-select v-model="row.next_role_code" placeholder="选择角色" style="width: 100%;">
              <el-option v-for="r in adminRoles" :key="r.role_code" :label="`${r.role_code} (${r.role_name})`" :value="r.role_code" />
            </el-select>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="120">
          <template #default="{ row }">
            <el-button type="primary" link @click="saveUserRole(row)">保存</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { Cpu, ChatDotRound, DataLine, Plus, Message, Close } from '@element-plus/icons-vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { login, register, validateSession, me, logout, adminListRoles, adminListUsers, adminSetUserRole } from './api'

const router = useRouter()
const route = useRoute()

const isLoggedIn = ref(false)
const authMode = ref('login')
const loginForm = reactive({ username: '', password: '', email: '' })
const sessions = ref([])
const authToken = ref('')
const permissions = ref([])
const userRoleDialogVisible = ref(false)
const adminRoles = ref([])
const adminUsers = ref([])

const getSessionsKey = (username) => {
  const u = (username && username.trim()) ? username.trim() : 'guest'
  return `rag_sessions_${u}`
}

const loadSessionsForCurrentUser = (username) => {
  const key = getSessionsKey(username)
  const saved = localStorage.getItem(key)
  if (saved) {
    sessions.value = JSON.parse(saved)
    return
  }

  // Optional migration: if legacy key exists for first-time per-user install
  const legacy = localStorage.getItem('rag_sessions')
  if (legacy) {
    sessions.value = JSON.parse(legacy)
    localStorage.setItem(key, legacy)
    // Move legacy data only once to avoid seeding every user with the same history.
    localStorage.removeItem('rag_sessions')
  } else {
    sessions.value = []
  }
}

onMounted(() => {
  const savedToken = localStorage.getItem('rag_auth_token')
  const savedUser = localStorage.getItem('rag_auth_user')
  if (savedToken) {
    authToken.value = savedToken
    validateSession(savedToken).then((res) => {
      isLoggedIn.value = true
      loginForm.username = res.data?.data?.username || savedUser || ''
      me().then((r) => {
        permissions.value = r.data?.data?.permissions || []
      }).catch(() => {
        permissions.value = []
      })

      // Load per-user sessions after username is known
      loadSessionsForCurrentUser(loginForm.username)
    }).catch(() => {
      localStorage.removeItem('rag_auth_token')
      localStorage.removeItem('rag_auth_user')
      isLoggedIn.value = false
      authToken.value = ''
    })
  }
})

const syncSessions = (newSessions) => {
  sessions.value = [...newSessions]
  localStorage.setItem(getSessionsKey(loginForm.username), JSON.stringify(sessions.value))
}

const handleNewChat = () => {
  router.push('/')
}

const handleDeleteSession = (id) => {
  sessions.value = sessions.value.filter(s => String(s.id) !== String(id))
  localStorage.setItem(getSessionsKey(loginForm.username), JSON.stringify(sessions.value))
  if (route.path.includes(id)) router.push('/')
}

const handleLogin = async () => {
  if (loginForm.username && loginForm.password) {
    try {
      const res = await login(loginForm.username, loginForm.password)
      const token = res.data?.data?.token
      const username = res.data?.data?.username || loginForm.username
      if (!token) throw new Error('missing token')
      authToken.value = token
      isLoggedIn.value = true
      loginForm.username = username
      localStorage.setItem('rag_auth_token', token)
      localStorage.setItem('rag_auth_user', username)
      const m = await me()
      permissions.value = m.data?.data?.permissions || []
      loadSessionsForCurrentUser(username)
      ElMessage.success('欢迎回来')
    } catch (e) {
      ElMessage.error('登录失败，请检查账号和密码')
    }
  } else {
    ElMessage.warning('请输入用户名和密码')
  }
}

const handleRegister = async () => {
  if (!loginForm.username || !loginForm.password || !loginForm.email) {
    ElMessage.warning('请输入用户名、邮箱和密码')
    return
  }
  try {
    await register(loginForm.username, loginForm.password, loginForm.email)
    ElMessage.success('注册成功，正在登录')
    await handleLogin()
  } catch (e) {
    ElMessage.error('注册失败，用户名可能已存在')
  }
}

const handleLogout = async () => {
  ElMessageBox.confirm('确定退出吗？', '提示').then(async () => {
    const token = authToken.value || localStorage.getItem('rag_auth_token') || ''
    try { await logout(token) } catch (e) {}
    isLoggedIn.value = false
    authToken.value = ''
    permissions.value = []
    loginForm.password = ''
    sessions.value = []
    localStorage.removeItem('rag_auth_token')
    localStorage.removeItem('rag_auth_user')
  })
}

const can = (perm) => {
  return permissions.value.includes(perm)
}

const loadAdminUserRoleData = async () => {
  const [rolesRes, usersRes] = await Promise.all([adminListRoles(), adminListUsers()])
  adminRoles.value = rolesRes.data?.data || []
  adminUsers.value = (usersRes.data?.data || []).map(u => ({
    ...u,
    next_role_code: (u.role_codes && u.role_codes.length > 0) ? u.role_codes[0] : ''
  }))
}

const openUserRoleDialog = async () => {
  try {
    await loadAdminUserRoleData()
    userRoleDialogVisible.value = true
  } catch (e) {
    ElMessage.error('加载用户角色数据失败')
  }
}

const saveUserRole = async (row) => {
  if (!row.next_role_code) {
    ElMessage.warning('请先选择角色')
    return
  }
  try {
    await adminSetUserRole(row.id, row.next_role_code)
    ElMessage.success('角色更新成功')
    await loadAdminUserRoleData()
  } catch (e) {
    ElMessage.error('角色更新失败')
  }
}
</script>

<style>
/* 核心修复：必须撑开根节点，否则 el-main 高度为 0 */
html, body, #app {
  height: 100%;
  margin: 0;
  padding: 0;
  overflow: hidden;
}
</style>

<style scoped>
.rag-layout { height: 100vh; width: 100vw; display: flex; }
.full-height-container { height: 100%; width: 100%; }
.login-wrapper { height: 100vh; width: 100vw; display: flex; justify-content: center; align-items: center; background-color: #001529; }
.login-card { width: 380px; background: white; padding: 40px; border-radius: 12px; }
.system-sidebar { background-color: #001529; display: flex; flex-direction: column; }
.brand { padding: 25px; color: white; display: flex; align-items: center; gap: 12px; background: #002140; font-weight: bold; }
.sidebar-menu { border-right: none; flex: 1; overflow-y: auto; }
.main-content { padding: 0 !important; background: #fff; height: 100%; position: relative; }
.sidebar-footer { padding: 15px 20px; background: #001529; border-top: 1px solid #002140; display: flex; align-items: center; justify-content: space-between; }
.user-info { display: flex; align-items: center; gap: 8px; color: #fff; }
.session-title { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.delete-btn { color: #ff4d4f; opacity: 0; cursor: pointer; }
.history-item:hover .delete-btn { opacity: 1; }
</style>