import { createRouter, createWebHistory } from 'vue-router'
import ChatView from '../components/ChatView.vue'
import EvalView from '../components/EvalView.vue'

const routes = [
  { path: '/', name: 'Chat', component: ChatView },
  { path: '/chat/:id', name: 'ChatWithId', component: ChatView },
  { path: '/eval', name: 'Evaluation', component: EvalView }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

export default router