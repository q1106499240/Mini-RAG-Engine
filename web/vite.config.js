import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  server: {
    proxy: {
      '/api': {
        target: 'http://localhost:8080', 
        changeOrigin: true,
        // --- 新增配置：禁用代理缓冲区 ---
        buffer: false, 
        // 某些版本的 Vite 可能需要此设置
        proxyTimeout: 60000, 
        timeout: 60000,
        rewrite: (path) => path.replace(/^\/api/, '')
      }
    }
  }
})