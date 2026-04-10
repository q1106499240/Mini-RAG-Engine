# CMakeLists.txt for llm_rag_cpp
cmake_minimum_required (VERSION 3.10)

# If supported, enable MSVC hot reload.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

if(MSVC)
    add_compile_options("/utf-8")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

project ("llm_rag_cpp")

# --- 1. Dependencies ---
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED) 

# --- 2. ONNX Runtime paths ---
set(ONNX_ROOT "E:/C++/code/llm_rag_cpp/Xenova/onnxruntime-win-x64-1.24.4")

# --- 3. Build SQLite3 static library (with FTS5) ---
add_library(sqlite3 STATIC 
    "database/sqlite3.c"
    "database/sqlite3.h"
    "database/sqlite_db.cpp" 
    "database/sqlite_db.h"
)

# Ensure sqlite3.c is compiled as C (not C++) under MSVC/IDE generators.
set_source_files_properties("database/sqlite3.c" PROPERTIES LANGUAGE C)

if(MSVC)
    # Force C mode for sqlite3 amalgamation to keep IntelliSense/compiler aligned.
    set_source_files_properties("database/sqlite3.c" PROPERTIES COMPILE_FLAGS "/TC")
endif()

# Enable full-text search features
target_compile_definitions(sqlite3 PUBLIC 
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_FTS4
    SQLITE_ENABLE_FTS3
    SQLITE_ENABLE_RTREE
)

# --- 4. 定义主可执行文件及其源代码 ---
add_executable (llm_rag_cpp 
    "llm_rag_cpp.cpp" 
    "llm_rag_cpp.h" 
    "cli/cli.h" 
    "cli/cli.cpp" 
    "llm/llm_client.h" 
    "llm/llm_client.cpp" 
    "rag/loader.h" 
    "rag/loader.cpp" 
    "rag/retriever.h" 
    "rag/retriever.cpp" 
    "rag/prompt.h" 
    "rag/prompt.cpp" 
    "rag/embedder.h" 
    "rag/embedder.cpp" 
    "rag/vector_utils.h" 
    "rag/vector_utils.cpp" 
    "rag/vector_retriever.h" 
    "rag/vector_retriever.cpp" 
    "rag/bm25_retriever.h" 
    "rag/bm25_retriever.cpp" 
    "rag/hybrid_retriever.h" 
    "rag/hybrid_retriever.cpp"
    "rag/sqlite_retriever.h" 
    "rag/sqlite_retriever.cpp"  # 检索逻辑
    "auth/auth_service.h"
    "auth/auth_service.cpp"
    "auth/password_hasher.h"
    "auth/password_hasher.cpp"
    "auth/rbac_service.h"
    "auth/rbac_service.cpp"
    "auth/http_auth.h"
    "auth/http_auth.cpp"
    "include/httplib.h"
)

# --- 5. 配置包含目录 (Header Files) ---
target_include_directories(llm_rag_cpp PRIVATE 
    "${ONNX_ROOT}/include"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/database" # 确保能找到 sqlite3.h
    "${CMAKE_SOURCE_DIR}/rag"
)

# --- 6. 配置库目录 (Library Files) ---
target_link_directories(llm_rag_cpp PRIVATE 
    "${ONNX_ROOT}/lib"
)

# --- 7. 链接依赖库 ---
target_link_libraries(llm_rag_cpp PRIVATE
    sqlite3                       # 链接刚才定义的 sqlite3 库（包含 FTS5）
    CURL::libcurl
    nlohmann_json::nlohmann_json
    onnxruntime.lib
    Threads::Threads              
)

# --- 链接 OpenMP 库 ---
if(OpenMP_CXX_FOUND)
    target_link_libraries(llm_rag_cpp PRIVATE OpenMP::OpenMP_CXX)
endif()

# --- 8. 设置项目属性 ---
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET llm_rag_cpp PROPERTY CXX_STANDARD 20)
endif()

# 确保 MSVC 下能够正确处理多线程运行时
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# --- 9. 文件自动拷贝逻辑 ---
# 拷贝知识库
if(EXISTS "${CMAKE_SOURCE_DIR}/rag/knowledge.txt")
    configure_file(
        ${CMAKE_SOURCE_DIR}/rag/knowledge.txt
        ${CMAKE_BINARY_DIR}/knowledge.txt
        COPYONLY
    )
endif()

# 自动将 onnxruntime.dll 拷贝到 exe 所在的运行目录
add_custom_command(TARGET llm_rag_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${ONNX_ROOT}/lib/onnxruntime.dll"
    $<TARGET_FILE_DIR:llm_rag_cpp>
)