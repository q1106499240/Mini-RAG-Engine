from flask import Flask, request, jsonify
from sentence_transformers import SentenceTransformer
import torch

app = Flask(__name__)

# 1. 检查 CUDA 是否可用
device = "cuda" if torch.cuda.is_available() else "cpu"
print(f"--- [INFO] Loading Model on: {device} ---")

# 2. 加载模型并设置为推理模式
model = SentenceTransformer('BAAI/bge-small-en-v1.5', device=device)
model.eval() # 设置为评估模式

# 3. 预热 (Warm-up)：防止第一次检索时卡顿
print("[INFO] Warming up GPU...")
with torch.no_grad():
    model.encode("warmup query")
print("[INFO] RAG Vector Server is ready!")

@app.route('/embed', methods=['POST'])
def embed():
    text = request.json.get('text', '')
    if not text:
        return jsonify({"embedding": []})
    
    # 4. 显式关闭梯度计算，大幅提升推理速度
    with torch.no_grad():
        # normalize_embeddings=True 保证余弦相似度计算更准确
        embedding = model.encode(text, normalize_embeddings=True).tolist()
        
    return jsonify({"embedding": embedding})

if __name__ == '__main__':
    # threaded=True 允许处理并发请求
    app.run(port=5000, host='0.0.0.0', threaded=True)