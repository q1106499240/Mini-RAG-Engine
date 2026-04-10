// cli/cli.cpp - Interactive CLI (legacy/debug)
#include <iostream>
#include <string>
#include "cli.h"
#include "../llm/llm_client.h"
#include <windows.h>
#include "../rag/loader.h"
#include "../rag/retriever.h"
#include "../rag/prompt.h"
#include <vector>
#include "../rag/vector_retriever.h"

using namespace std;

extern std::vector<VecChunk> global_chunks;

void start_cli() {
    std::cout << "CLI started. Type 'exit' to quit." << std::endl;
    std::string command;

    // Convert UTF-8 to CP_ACP for Windows console
    auto utf8_to_mbcs = [](const std::string& utf8) -> std::string {
        if (utf8.empty()) return std::string{};
        int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
        if (wlen == 0) return utf8;
        std::wstring w;
        w.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &w[0], wlen);
        int len = WideCharToMultiByte(CP_ACP, 0, w.data(), wlen, nullptr, 0, nullptr, nullptr);
        if (len == 0) return utf8;
        std::string out;
        out.resize(len);
        WideCharToMultiByte(CP_ACP, 0, w.data(), wlen, &out[0], len, nullptr, nullptr);
        return out;
    };

    while (true) {
        cout << "\nCommands: ask | load | exit" << endl;
        std::cout << "> ";
        std::getline(std::cin, command);

        if (command == "exit") {
            cout << "Exiting CLI. Goodbye!" << endl;
            break;
        }
        else if (command == "ask") {
            cout << "Mode: ask" << std::endl;

            cout << "[Debug] Checking index state..." << endl;
            cout << "[Debug] In-memory chunk count: " << global_chunks.size() << endl;

            if (global_chunks.empty()) {
                cout << "[Warning] Chunk cache is empty. Run 'load' first." << endl;
            }

            string question;
            cout << "Question: ";
            std::getline(std::cin, question);

            auto context = vector_retrieve(question, 5);
            auto prompt = build_prompt(context, question);
            auto answer = call_llm(prompt);

            cout << "\nRetrieved context:\n" << context << endl;
            cout << "\nAnswer:\n" << utf8_to_mbcs(answer) << endl;
        }
        else if (command == "load") {
            cout << "Mode: load" << std::endl;
            //auto docs = load_documents("knowledge.txt");
			auto docs = load_documents("E:/C++/code/llm_rag_cpp/language.txt");
            cout << "\nLoaded " << docs.size() << " chunks." << endl;
            build_index(docs);
            save_index("index.dat");
            cout << "Vector index built and saved to index.dat" << endl;
        }
        else {
            std::cout << "Unknown command." << std::endl;
        }
    }
}