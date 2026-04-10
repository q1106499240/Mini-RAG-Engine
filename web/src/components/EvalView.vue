<template>
  <div class="eval-dashboard">
    <el-row :gutter="20" class="stat-row">
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <el-statistic title="测试样本总数" :value="evalResults.total_samples">
            <template #suffix><el-icon><Files /></el-icon></template>
          </el-statistic>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card hit-rate">
          <el-statistic title="Hit@1 (首位命中率)" :value="evalResults.hit_at_1 * 100" :precision="2">
            <template #suffix>%</template>
          </el-statistic>
          <div class="stat-footer">目标 > 60%</div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card mrr-score">
          <el-statistic title="MRR (排序得分)" :value="evalResults.mrr" :precision="4" />
          <div class="stat-footer">反映排名靠前程度</div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <el-statistic title="平均检索耗时" :value="evalResults.avg_latency_ms" :precision="2">
            <template #suffix>ms</template>
          </el-statistic>
        </el-card>
      </el-col>
    </el-row>

    <el-card class="main-eval-box">
      <div class="ctrl-header">
        <div class="title-group">
          <h3>引擎性能实时监控面板</h3>
          <el-tag size="small" type="success">Backend: C++ High-Performance Core</el-tag>
        </div>
        <div class="btns">
          <el-button 
            type="primary" 
            size="large" 
            :loading="isEvaluating" 
            @click="startEvaluation" 
            :icon="VideoPlay"
          >
            一键全量评测
          </el-button>
          <el-button @click="resetEval">重置视图</el-button>
        </div>
      </div>

      <el-row :gutter="20" v-if="evalResults.total_samples > 0">
        <el-col :span="12">
          <div ref="hitGaugeRef" style="height: 350px;"></div>
        </el-col>
        <el-col :span="12">
          <div ref="mrrGaugeRef" style="height: 350px;"></div>
        </el-col>
      </el-row>
      
      <el-empty v-else description="暂无评测数据，请点击开始评测" />
    </el-card>

    <el-row :gutter="20" style="margin-top: 20px;">
      <el-col :span="24">
        <el-card header="系统资源与性能分析">
          <div class="analysis-text" v-if="evalResults.total_samples > 0">
            系统本次评测共处理 <strong>{{ evalResults.total_samples }}</strong> 条数据，
            平均响应时间为 <strong>{{ evalResults.avg_latency_ms.toFixed(2) }}ms</strong>。
            召回表现：Hit@3 达到了 <strong>{{ (evalResults.hit_at_3 * 100).toFixed(2) }}%</strong>。
          </div>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, watch } from 'vue'
import { VideoPlay, Files } from '@element-plus/icons-vue'
import * as echarts from 'echarts'
import { runFullEvaluation } from '../api' // 确保 api/index.js 已定义此函数
import { ElMessage } from 'element-plus'

// --- 状态管理 ---
const isEvaluating = ref(false)
const hitGaugeRef = ref(null)
const mrrGaugeRef = ref(null)
let hitChart = null
let mrrChart = null

const evalResults = reactive({
  total_samples: 0,
  hit_at_1: 0,
  hit_at_3: 0,
  mrr: 0,
  avg_latency_ms: 0
})

// --- 图表逻辑 ---
const initCharts = () => {
  if (hitGaugeRef.value) {
    hitChart = echarts.init(hitGaugeRef.value)
    mrrChart = echarts.init(mrrGaugeRef.value)
    renderGauges()
  }
}

const renderGauges = () => {
  // Hit Rate 仪表盘
  hitChart?.setOption({
    title: { text: 'Hit@3 召回率', left: 'center', top: '30%' },
    series: [{
      type: 'gauge',
      startAngle: 180,
      endAngle: 0,
      min: 0,
      max: 100,
      progress: { show: true, width: 18, itemStyle: { color: '#67C23A' } },
      axisLine: { lineStyle: { width: 18 } },
      axisTick: { show: false },
      splitLine: { show: false },
      axisLabel: { show: false },
      pointer: { show: false },
      anchor: { show: false },
      title: { show: false },
      detail: {
        valueAnimation: true,
        width: '60%',
        lineHeight: 40,
        borderRadius: 8,
        offsetCenter: [0, '-15%'],
        fontSize: 30,
        fontWeight: 'bolder',
        formatter: '{value}%',
        color: 'inherit'
      },
      data: [{ value: (evalResults.hit_at_3 * 100).toFixed(1) }]
    }]
  })

  // MRR 仪表盘
  mrrChart?.setOption({
    title: { text: 'MRR 排序得分', left: 'center', top: '30%' },
    series: [{
      type: 'gauge',
      startAngle: 180,
      endAngle: 0,
      min: 0,
      max: 1,
      progress: { show: true, width: 18, itemStyle: { color: '#409EFF' } },
      axisLine: { lineStyle: { width: 18 } },
      axisTick: { show: false },
      splitLine: { show: false },
      axisLabel: { show: false },
      pointer: { show: false },
      anchor: { show: false },
      title: { show: false },
      detail: {
        valueAnimation: true,
        width: '60%',
        lineHeight: 40,
        borderRadius: 8,
        offsetCenter: [0, '-15%'],
        fontSize: 30,
        fontWeight: 'bolder',
        formatter: '{value}',
        color: 'inherit'
      },
      data: [{ value: evalResults.mrr.toFixed(3) }]
    }]
  })
}

// 监听数据变化刷新图表
watch(() => evalResults.total_samples, () => {
  setTimeout(initCharts, 100) // 确保 DOM 已渲染
})

// --- 业务逻辑 ---
const startEvaluation = async () => {
  isEvaluating.value = true
  try {
    const res = await runFullEvaluation()
    // 将后端返回的字段同步到响应式对象
    Object.assign(evalResults, res.data)
    ElMessage.success('系统评估圆满完成')
  } catch (err) {
    console.error(err)
    ElMessage.error('评测请求失败，请检查后端服务')
  } finally {
    isEvaluating.value = false
  }
}

const resetEval = () => {
  Object.assign(evalResults, {
    total_samples: 0,
    hit_at_1: 0,
    hit_at_3: 0,
    mrr: 0,
    avg_latency_ms: 0
  })
}

onMounted(() => {
  window.addEventListener('resize', () => {
    hitChart?.resize()
    mrrChart?.resize()
  })
})
</script>

<style scoped>
.eval-dashboard { padding: 20px; background: #f0f2f5; min-height: 100vh; }
.stat-card { border-radius: 12px; }
.stat-footer { font-size: 12px; color: #909399; margin-top: 8px; }

.hit-rate :deep(.el-statistic__content) { color: #67C23A; }
.mrr-score :deep(.el-statistic__content) { color: #409EFF; }

.main-eval-box { margin-top: 20px; border-radius: 12px; }
.ctrl-header { 
  display: flex; 
  justify-content: space-between; 
  align-items: center; 
  margin-bottom: 20px; 
  padding-bottom: 15px;
  border-bottom: 1px solid #ebeef5;
}
.title-group h3 { margin: 0 0 5px 0; color: #303133; }

.analysis-text {
  font-size: 16px;
  line-height: 1.8;
  color: #606266;
}
.analysis-text strong {
  color: #409EFF;
  margin: 0 4px;
}
</style>