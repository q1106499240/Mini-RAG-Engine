import struct
import os
from sentence_transformers import SentenceTransformer

# 1. 配置路径
INPUT_FILE = "doc/language.txt"  # 或者是你的 language.txt，请确保路径正确
OUTPUT_INDEX = "index.dat"

# 2. 加载模型
print("Loading BGE model...")
model = SentenceTransformer('BAAI/bge-small-en-v1.5', device='cuda')

# 3. 读取大文件并切分 (每 500 字符切一块)
def get_chunks(text, size=500):
    return [text[i:i+size] for i in range(0, len(text), size)]

if os.path.exists(INPUT_FILE):
    with open(INPUT_FILE, "r", encoding="utf-8", errors="ignore") as f:
        full_text = f.read()
    chunks = get_chunks(full_text)
else:
    # 如果没找到文件，先用这两句测试，确保 C++ 能搜到
    chunks = [
        "Email marketing is the act of sending a commercial message, typically to a group of people, using email.",
        "The main purposes of email marketing are to build loyalty, trust, or brand awareness."
    ]

# 4. 生成向量
print(f"Encoding {len(chunks)} chunks...")
embeddings = model.encode(chunks, normalize_embeddings=True)

# 5. 写入二进制文件
with open(OUTPUT_INDEX, "wb") as f:
    f.write(struct.pack("Q", len(chunks))) # 写入总块数
    for text, vec in zip(chunks, embeddings):
        t_bytes = text.encode('utf-8')
        f.write(struct.pack("Q", len(t_bytes))) # 文本长度
        f.write(t_bytes) # 文本内容
        f.write(struct.pack("Q", len(vec))) # 向量长度 (384)
        f.write(struct.pack(f"{len(vec)}f", *vec)) # 向量数据

print(f"Done! {len(chunks)} chunks saved to {OUTPUT_INDEX}")