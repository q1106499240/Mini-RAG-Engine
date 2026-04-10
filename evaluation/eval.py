# evaluation/eval.py
import requests
import time

API_URL = "http://127.0.0.1:8000/ask"

# 测试集示例：问题和参考答案
TEST_SET = [
    ("中国首都是哪里？", "北京"),
    ("法国的首都是哪座城市？", "巴黎"),
    ("长江是中国最长的河流吗？", "长江"),
    ("人民币是中国的法定货币吗？", "人民币"),
    ("Python 是什么语言？", "编程语言"),
]

def simple_keyword_accuracy(answer: str, reference: str):
    """
    简单评测函数：参考答案关键字是否在模型回答中
    """
    return 1 if reference in answer else 0

def evaluate():
    total = len(TEST_SET)
    correct = 0
    total_latency = 0

    for q, ref in TEST_SET:
        start = time.time()
        response = requests.post(API_URL, params={"query": q})
        latency = time.time() - start
        total_latency += latency

        answer = response.json().get("answer", "")
        hit = simple_keyword_accuracy(answer, ref)
        correct += hit

        print(f"Q: {q}")
        print(f"Answer: {answer}")
        print(f"Reference: {ref}")
        print(f"Hit: {hit}, Latency: {latency:.3f}s")
        print("-" * 50)

    accuracy = correct / total
    avg_latency = total_latency / total
    print(f"Final Accuracy: {accuracy*100:.2f}%")
    print(f"Average Latency: {avg_latency:.3f}s")

if __name__ == "__main__":
    evaluate()
