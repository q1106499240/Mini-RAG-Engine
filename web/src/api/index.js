import axios from 'axios'

const BASE_URL = '/api' 

const service = axios.create({
    baseURL: BASE_URL,
    timeout: 30000 
})

service.interceptors.request.use((config) => {
    const token = localStorage.getItem('rag_auth_token')
    if (token) {
        config.headers = config.headers || {}
        config.headers.Authorization = `Bearer ${token}`
    }
    return config
})

// --- 问答接口 ---
export const askQuestionStream = async (query) => {
    const token = localStorage.getItem('rag_auth_token')
    const headers = { 'Content-Type': 'application/json' }
    if (token) headers.Authorization = `Bearer ${token}`
    return fetch(`${BASE_URL}/ask`, {
        method: 'POST',
        headers,
        body: JSON.stringify({ query })
    });
}

// --- 评测接口 ---
// 触发 C++ 后端的全量评估逻辑
export const runFullEvaluation = () => {
    return service.get('/eval', { timeout: 180000 })
}

// 获取检索结果 (保留原有，用于单次测试)
export const retrieveDocs = (query) => {
    return service.get('/retrieve', { params: { query } })
}

// --- Auth interfaces ---
export const login = (username, password) => {
    return service.post('/auth/login', { username, password })
}

export const register = (username, password, email) => {
    return service.post('/auth/register', { username, password, email })
}

export const validateSession = (token) => {
    return service.get('/auth/session', { params: { token } })
}

export const me = () => {
    return service.get('/auth/me')
}

export const logout = (token) => {
    return service.post('/auth/logout', { token })
}

export const adminListRoles = () => {
    return service.get('/admin/roles')
}

export const adminListUsers = () => {
    return service.get('/admin/users')
}

export const adminSetUserRole = (user_id, role_code) => {
    return service.post('/admin/users/role', { user_id, role_code })
}

export default service