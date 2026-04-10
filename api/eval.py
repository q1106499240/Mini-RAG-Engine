import asyncio
import aiohttp
import json
import time
import os

current_dir = os.path.dirname(os.path.abspath(__file__))
file_path = os.path.join(current_dir, "dataset.json")

# 指向 C++ RAG 后端
BASE_URL = "http://localhost:8000"

def calculate_mrr(retrieved_docs, ground_truth):
    """
    计算平均倒数排名 (MRR)
    """
    if not retrieved_docs:
        return 0.0
    for rank, doc in enumerate(retrieved_docs, start=1):
        if ground_truth.strip().lower() in doc.lower():
            return 1.0 / rank
    return 0.0

async def evaluate_item(session, item):
    """
    单个测试用例的异步请求处理
    """
    query = item["query"]
    ground_truth = item["answer"]

    try:
        # 注意：评测检索效果应调用 /retrieve 接口
        async with session.get(f"{BASE_URL}/retrieve", params={"query": query}, timeout=10) as response:
            if response.status != 200:
                print(f"请求失败: {query}, Status: {response.status}")
                return 0, 0.0
            
            data = await response.json()
            # 修正语法错误：使用逗号而非冒号
            retrieved = data.get("retrieved", [])
            
            # 修正拼写错误：isinstance
            if isinstance(retrieved, str):
                retrieved = retrieved.split("\n")

            # 1. 计算 Hit@K
            is_hit = 1 if any(ground_truth.strip().lower() in d.lower() for d in retrieved) else 0

            # 2. 计算 MRR
            mrr_score = calculate_mrr(retrieved, ground_truth)

            return is_hit, mrr_score
    except Exception as e:
        print(f"Error evaluating query [{query}]: {e}")
        return 0, 0.0

async def run_evaluation():
    # 读取数据集 (确保文件名正确)
    file_name = "dataset.json" # 如果你的文件名是 eval_dataset.json 请修改此处
    try:
        with open(file_name, "r", encoding="utf-8") as f:
            dataset = json.load(f)
    except FileNotFoundError:
        print(f"评测数据集文件 {file_name} 未找到。")
        return

    start_time = time.time()

    async with aiohttp.ClientSession() as session:
        # 并发执行所有评估任务
        tasks = [evaluate_item(session, item) for item in dataset]
        results = await asyncio.gather(*tasks)

    # 统计结果
    total = len(dataset)
    if total == 0:
        print("数据集为空")
        return

    total_hits = sum(r[0] for r in results)
    total_mrr = sum(r[1] for r in results)

    duration = time.time() - start_time

    print("\n" + "="*20 + " RAG EVALUATION REPORT " + "="*20)
    print(f"测试样本总数: {total}")
    print(f"总计耗时:     {duration:.2f} 秒")
    print(f"平均每条耗时: {duration/total:.4f} 秒")
    print("-" * 50)
    print(f"Hit@K (召回率): {total_hits / total:.4f}")
    print(f"MRR (排序质量): {total_mrr / total:.4f}")
    print("="*63)

if __name__ == "__main__":
    asyncio.run(run_evaluation())