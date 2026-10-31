#include "core/ShaderCompiler.h"
#include <windows.h> // OutputDebugStringW
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_set>
#include <iterator>

using namespace ge3::core;

static void DebugPrint(const wchar_t* msg) {
#if defined(_DEBUG)
    OutputDebugStringW(msg);
#endif
}

static void LogShaderCompileError(
    const std::wstring& filePath,
    const std::wstring& targetProfile,
    const char* details) {
    std::error_code error;
    std::filesystem::create_directories("logs", error);
    std::ofstream output("logs/shader_compile_errors.log", std::ios::app);
    if (!output) {
        return;
    }
    output << "file=" << std::filesystem::path(filePath).string()
           << " profile=" << std::filesystem::path(targetProfile).string() << '\n';
    if (details != nullptr && details[0] != '\0') {
        output << details;
        if (details[std::char_traits<char>::length(details) - 1] != '\n') {
            output << '\n';
        }
    }
}

namespace {
uint64_t HashAppend(uint64_t hash, const void* data, size_t size) {
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
    return hash;
}

uint64_t HashAppendString(uint64_t hash, const std::wstring& value) {
    return HashAppend(hash, value.data(), value.size() * sizeof(wchar_t));
}

uint64_t HashAppendString(uint64_t hash, const std::string& value) {
    return HashAppend(hash, value.data(), value.size());
}

uint64_t HashShaderSourceTree(uint64_t hash,const std::filesystem::path& path,
    std::unordered_set<std::wstring>& visited) {
    std::error_code error;
    const auto normalized=std::filesystem::weakly_canonical(path,error).lexically_normal();
    if(!visited.insert(normalized.wstring()).second)return hash;
    hash=HashAppendString(hash,normalized.wstring());
    std::ifstream source(path,std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(source),std::istreambuf_iterator<char>()};
    hash=HashAppend(hash,contents.data(),contents.size());
    size_t offset=0;
    while((offset=contents.find("#include",offset))!=std::string::npos) {
        const size_t lineEnd=contents.find('\n',offset),begin=contents.find('"',offset+8);
        if(begin==std::string::npos || (lineEnd!=std::string::npos && begin>lineEnd)){offset+=8;continue;}
        const size_t end=contents.find('"',begin+1);if(end==std::string::npos)break;
        hash=HashShaderSourceTree(hash,path.parent_path()/contents.substr(begin+1,end-begin-1),visited);
        offset=end+1;
    }
    return hash;
}

std::filesystem::path ShaderBytecodeCachePath(
    const std::wstring& filePath,
    const std::wstring& entryPoint,
    const std::wstring& targetProfile,
    const std::vector<LPCWSTR>& extraArgs) {
    constexpr uint64_t kFnvOffset = 1469598103934665603ull;
    std::error_code error;
    const std::filesystem::path normalized =
        std::filesystem::weakly_canonical(filePath, error).lexically_normal();
    const auto writeTime = std::filesystem::last_write_time(filePath, error);
    const uint64_t writeTimeTicks = error
        ? 0ull
        : static_cast<uint64_t>(writeTime.time_since_epoch().count());
    error.clear();
    const uint64_t sourceSize = std::filesystem::file_size(filePath, error);

    uint64_t hash = kFnvOffset;
    hash=HashAppendString(hash,std::string("source-tree-v2"));
    hash = HashAppendString(hash, normalized.wstring());
    hash = HashAppendString(hash, entryPoint);
    hash = HashAppendString(hash, targetProfile);
#if _DEBUG
    hash = HashAppendString(hash, std::string("Debug"));
#else
    hash = HashAppendString(hash, std::string("Release"));
#endif
    hash = HashAppend(hash, &writeTimeTicks, sizeof(writeTimeTicks));
    hash = HashAppend(hash, &sourceSize, sizeof(sourceSize));
    for (LPCWSTR arg : extraArgs) {
        hash = HashAppendString(hash, std::wstring(arg != nullptr ? arg : L""));
    }
    // AppPipelines has its own include-aware cache. The lower compiler must
    // not return old bytecode when only a nested .hlsli changed.
    std::unordered_set<std::wstring> visited;
    hash=HashShaderSourceTree(hash,std::filesystem::path(filePath),visited);

    char fileName[48]{};
    std::snprintf(fileName, sizeof(fileName), "%016llx.dxil", static_cast<unsigned long long>(hash));
    return std::filesystem::path("cache") / "shaders" / fileName;
}

ComPtr<IDxcBlob> LoadCachedShaderBytecode(
    IDxcUtils* utils,
    const std::filesystem::path& cachePath) {
    if (utils == nullptr) {
        return nullptr;
    }

    std::error_code error;
    const uint64_t byteCount = std::filesystem::file_size(cachePath, error);
    if (error || byteCount == 0 || byteCount > 64ull * 1024ull * 1024ull) {
        return nullptr;
    }

    std::ifstream input(cachePath, std::ios::binary);
    if (!input) {
        return nullptr;
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return nullptr;
    }

    ComPtr<IDxcBlobEncoding> encoded;
    if (FAILED(utils->CreateBlob(
            bytes.data(),
            static_cast<UINT32>(bytes.size()),
            DXC_CP_ACP,
            &encoded)) ||
        encoded == nullptr) {
        return nullptr;
    }

    ComPtr<IDxcBlob> blob;
    if (FAILED(encoded.As(&blob))) {
        return nullptr;
    }
    return blob;
}

void StoreCachedShaderBytecode(
    const std::filesystem::path& cachePath,
    const ComPtr<IDxcBlob>& blob) {
    if (blob == nullptr || blob->GetBufferPointer() == nullptr || blob->GetBufferSize() == 0) {
        return;
    }

    std::error_code error;
    std::filesystem::create_directories(cachePath.parent_path(), error);
    if (error) {
        return;
    }

    std::ofstream output(cachePath, std::ios::binary | std::ios::trunc);
    if (!output) {
        return;
    }
    output.write(
        static_cast<const char*>(blob->GetBufferPointer()),
        static_cast<std::streamsize>(blob->GetBufferSize()));
}
} // namespace

bool ShaderCompiler::Initialize() {
    lastError_ = S_OK;

    if (utils_ || compiler_) {
        // 既に初期化済み
        return true;
    }

    lastError_ = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils_));
    if (FAILED(lastError_)) {
        DebugPrint(L"[ShaderCompiler] DxcCreateInstance(DxcUtils) failed\n");
        return false;
    }

    lastError_ = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_));
    if (FAILED(lastError_)) {
        DebugPrint(L"[ShaderCompiler] DxcCreateInstance(DxcCompiler) failed\n");
        utils_.Reset();
        return false;
    }

    lastError_ = utils_->CreateDefaultIncludeHandler(&includeHandler_);
    if (FAILED(lastError_)) {
        DebugPrint(L"[ShaderCompiler] CreateDefaultIncludeHandler failed\n");
        compiler_.Reset();
        utils_.Reset();
        return false;
    }

    DebugPrint(L"[ShaderCompiler] Initialize OK\n");
    return true;
}

void ShaderCompiler::Finalize() {
    includeHandler_.Reset();
    compiler_.Reset();
    utils_.Reset();
}

Microsoft::WRL::ComPtr<IDxcBlob> ShaderCompiler::CompileFromFile(
    const std::wstring& filePath,
    const std::wstring& entryPoint,
    const std::wstring& targetProfile,
    const std::vector<LPCWSTR>& extraArgs)
{
    ComPtr<IDxcBlob> resultBlob;
    lastError_ = S_OK;

    if (!utils_ || !compiler_) {
        DebugPrint(L"[ShaderCompiler] CompileFromFile called before Initialize\n");
        lastError_ = E_FAIL;
        return resultBlob;
    }

    if (!std::filesystem::exists(filePath)) {
        DebugPrint(L"[ShaderCompiler] file not found: ");
        DebugPrint(filePath.c_str());
        DebugPrint(L"\n");
        lastError_ = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        return resultBlob;
    }

    const std::filesystem::path cachePath =
        ShaderBytecodeCachePath(filePath, entryPoint, targetProfile, extraArgs);
    if (ComPtr<IDxcBlob> cached = LoadCachedShaderBytecode(utils_.Get(), cachePath)) {
        return cached;
    }

    // ファイル読み込み → DxcBuffer 準備
    ComPtr<IDxcBlobEncoding> sourceBlob;
    lastError_ = utils_->LoadFile(filePath.c_str(), nullptr, &sourceBlob);
    if (FAILED(lastError_)) {
        DebugPrint(L"[ShaderCompiler] LoadFile failed\n");
        return resultBlob;
    }

    DxcBuffer sourceBuffer{};
    sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
    sourceBuffer.Size = sourceBlob->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_ACP; // 既に UTF-8 / UTF-16 に対応済みの想定

    // 引数を組み立て
    std::vector<LPCWSTR> args;
    args.reserve(8 + extraArgs.size());

    args.push_back(filePath.c_str());       // ソース名
    args.push_back(L"-E");                  // エントリポイント
    args.push_back(entryPoint.c_str());
    args.push_back(L"-T");                  // ターゲット
    args.push_back(targetProfile.c_str());

    // デバッグ情報（必要に応じて調整）
#if _DEBUG
    args.push_back(L"-Zi");                // デバッグ情報
    args.push_back(L"-Qembed_debug");
    args.push_back(L"-Od");                // 最適化オフ
#else
    args.push_back(L"-O3");                // Release 時は最適化
#endif
    // ★ ここを追加：行優先レイアウトを強制
    args.push_back(L"-Zpr");
    for (auto a : extraArgs) {
        args.push_back(a);
    }

    ComPtr<IDxcResult> result;
    lastError_ = compiler_->Compile(
        &sourceBuffer,
        args.data(),
        (UINT)args.size(),
        includeHandler_.Get(),
        IID_PPV_ARGS(&result));

    if (FAILED(lastError_) || !result) {
        DebugPrint(L"[ShaderCompiler] Compile() failed\n");
        return ComPtr<IDxcBlob>();
    }

    // エラー出力があればログに出す
    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (errors && errors->GetStringLength() > 0) {
        DebugPrint(L"[ShaderCompiler] CompileFromFile errors:\n");
        OutputDebugStringA(errors->GetStringPointer());
        DebugPrint(L"\n");
    }

    HRESULT status = S_OK;
    result->GetStatus(&status);
    if (FAILED(status)) {
        DebugPrint(L"[ShaderCompiler] CompileFromFile status FAILED\n");
        LogShaderCompileError(
            filePath,
            targetProfile,
            errors ? errors->GetStringPointer() : "DXC compilation failed without diagnostics.");
        lastError_ = status;
        return ComPtr<IDxcBlob>();
    }

    // 実バイナリを取り出す
    ComPtr<IDxcBlob> shader;
    lastError_ = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader), nullptr);
    if (FAILED(lastError_) || !shader) {
        DebugPrint(L"[ShaderCompiler] GetOutput(DXC_OUT_OBJECT) failed\n");
        return ComPtr<IDxcBlob>();
    }

    StoreCachedShaderBytecode(cachePath, shader);
    return shader;
}
