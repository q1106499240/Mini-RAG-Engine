markdown
# Mini-RAG Engine

一个轻量级的 **RAG（Retrieval-Augmented Generation）** 引擎，使用 C++ 实现，支持多种检索策略（向量检索、BM25、混合检索）和 SQLite 后端存储。同时提供了 Python 辅助工具、Web 可视化界面以及 Agent 工具调用能力。

## ✨ 功能特性

- 🚀 **高效检索**：支持稠密向量检索、BM25 关键词检索及混合检索（Hybrid Search）
- 🗄️ **SQLite 存储**：文档切块后使用 SQLite 数据库持久化，便于管理和扩展
- 🤖 **Agent 工具**：集成简单的 Agent 框架，可调用外部工具（如计算器、搜索等）
- 🌐 **Web 界面**：基于 Vue3 的前端交互界面，支持对话式问答
- 🔌 **多模型支持**：可接入 OpenAI API / DeepSeek API 等 LLM 服务
- 🧩 **模块化设计**：核心组件（加载器、嵌入器、检索器、生成器）松耦合，易于定制

## 🛠️ 技术栈

| 组件            | 技术                                 |
| --------------- | ------------------------------------ |
| 核心语言        | C++17                                |
| 构建系统        | CMake + Ninja                        |
| 向量检索        | 自定义实现（支持余弦相似度）         |
| 关键词检索      | BM25 算法                            |
| 嵌入向量生成    | 调用 OpenAI / DeepSeek Embeddings API|
| 数据库          | SQLite3                              |
| Web 后端        | Python + Flask                       |
| 前端界面        | Vue3              |

## 📦 环境要求

- **编译器**：支持 C++17（MSVC 2019+ / GCC 9+ / Clang 10+）
- **CMake**：3.15 或更高版本
- **依赖库**：
  - nlohmann/json
  - cpp-httplib（或 libcurl）
  - sqlite3
- **Python**（可选，用于 Web 服务和辅助脚本）：3.8+

## 🔧 构建与安装

### 1. 克隆仓库

git clone https://github.com/q1106499240/Mini-RAG-Engine.git
cd Mini-RAG-Engine
2. 配置 CMake 构建

mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
3. 运行命令行示例
./llm_rag_cpp --query "什么是检索增强生成？"
🧪 使用指南
索引文档（构建知识库）
将文本文档放入 rag/knowledge.txt（每行一条知识），然后运行：

./llm_rag_cpp --index
程序会将文档切块，生成向量索引并存入 SQLite 数据库。

查询问答
./llm_rag_cpp --query "你的问题"
启动 Web 界面
cd web
python app.py
浏览器打开 http://localhost:5000 即可使用。

Agent 模式

python api/agent.py --tool calculator
📂 项目结构
text
Mini-RAG-Engine/
├── CMakeLists.txt          # 主构建文件
├── .gitignore              # 忽略规则
├── llm_rag_cpp.cpp/h       # 主入口
├── cli/                    # 命令行接口
├── rag/                    # 检索核心模块
│   ├── loader.cpp/h        # 文档加载与切块
│   ├── embedder.cpp/h      # 嵌入向量生成
│   ├── retriever.cpp/h     # 检索基类
│   ├── vector_retriever    # 向量检索
│   ├── bm25_retriever      # BM25检索
│   ├── hybrid_retriever    # 混合检索
│   └── sqlite_retriever    # SQLite存储
├── llm/                    # LLM 客户端
├── api/                    # Agent / 工具 / 评估脚本
├── web/                    # Web 前端
├── database/               # SQLite 数据库文件（运行时生成）
└── doc/                    # 文档与数据集
⚙️ 配置
创建 deepseek_api.txt 文件（不要提交到 Git），内容为你的 API 密钥：

your-api-key-here
或者通过环境变量设置：

export DEEPSEEK_API_KEY="your-key"
🧪 测试与评估

cd evaluation
python eval.py --dataset doc/ques_ans.csv
