#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <dwmapi.h>
#include <psapi.h>
#include <wrl/client.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

static std::string utf8(const wchar_t* text) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), count, nullptr, nullptr);
    result.pop_back();
    return result;
}
static std::string quoted(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char value : text) {
        if (value == '"' || value == '\\') out << '\\' << static_cast<char>(value);
        else if (value < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(value);
        else out << static_cast<char>(value);
    }
    out << '"';
    return out.str();
}
static std::string hr(HRESULT value) {
    std::ostringstream out;
    out << '"' << "0x" << std::hex << std::uppercase << std::setw(8)
        << std::setfill('0') << static_cast<std::uint32_t>(value) << '"';
    return out.str();
}
static double milliseconds(Clock::duration elapsed) {
    return std::chrono::duration<double, std::milli>(elapsed).count();
}
static void power() {
    SYSTEM_POWER_STATUS status{};
    const bool valid = GetSystemPowerStatus(&status) != FALSE;
    std::cout << "{\"valid\":" << valid << ",\"acLineStatus\":" << static_cast<int>(status.ACLineStatus)
        << ",\"batteryPercent\":" << static_cast<int>(status.BatteryLifePercent) << '}';
}
static void dwm() {
    DWM_TIMING_INFO info{};
    info.cbSize = sizeof(info);
    const HRESULT result = DwmGetCompositionTimingInfo(nullptr, &info);
    std::cout << "{\"hresult\":" << hr(result)
        << ",\"qpcVBlank\":" << info.qpcVBlank << ",\"qpcRefreshPeriod\":" << info.qpcRefreshPeriod
        << ",\"refreshCounter\":" << info.cRefresh
        << ",\"refreshNumerator\":" << info.rateRefresh.uiNumerator
        << ",\"refreshDenominator\":" << info.rateRefresh.uiDenominator
        << ",\"perApplicationDisplayFpsMeasured\":false}";
}
static void adapterInfo(IDXGIAdapter1* adapter) {
    DXGI_ADAPTER_DESC1 desc{};
    const HRESULT result = adapter->GetDesc1(&desc);
    LARGE_INTEGER version{};
    const HRESULT driver = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version);
    std::cout << "{\"hresult\":" << hr(result) << ",\"description\":" << quoted(utf8(desc.Description))
        << ",\"vendorId\":" << desc.VendorId << ",\"deviceId\":" << desc.DeviceId
        << ",\"luidHigh\":" << desc.AdapterLuid.HighPart << ",\"luidLow\":" << desc.AdapterLuid.LowPart
        << ",\"softwareFlag\":" << ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        << ",\"dedicatedVideoMemoryReported\":" << desc.DedicatedVideoMemory
        << ",\"sharedSystemMemoryReported\":" << desc.SharedSystemMemory
        << ",\"driverVersionHresult\":" << hr(driver) << ",\"driverVersion\":"
        << quoted(std::to_string(HIWORD(version.HighPart)) + '.' + std::to_string(LOWORD(version.HighPart))
            + '.' + std::to_string(HIWORD(version.LowPart)) + '.' + std::to_string(LOWORD(version.LowPart)));
    ComPtr<IDXGIAdapter3> budgetAdapter;
    const HRESULT qi = adapter->QueryInterface(IID_PPV_ARGS(&budgetAdapter));
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    const HRESULT budget = SUCCEEDED(qi) ? budgetAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory) : qi;
    std::cout << ",\"localBudget\":{\"hresult\":" << hr(budget) << ",\"bytes\":" << memory.Budget
        << ",\"processCurrentUsageBytes\":" << memory.CurrentUsage << ",\"snapshotOnly\":true}}";
}
static void formats(ID3D11Device* device) {
    struct Format { const char* name; DXGI_FORMAT value; bool renderTarget; };
    const Format list[] = {{"BGRA8_UNORM", DXGI_FORMAT_B8G8R8A8_UNORM, true},
        {"RGBA8_UNORM", DXGI_FORMAT_R8G8B8A8_UNORM, true}, {"R8_UINT", DXGI_FORMAT_R8_UINT, false},
        {"R8_UNORM", DXGI_FORMAT_R8_UNORM, true}, {"R32_UINT", DXGI_FORMAT_R32_UINT, false},
        {"R32_FLOAT", DXGI_FORMAT_R32_FLOAT, true}};
    std::cout << '[';
    bool first = true;
    for (const auto& format : list) {
        if (!first) std::cout << ',';
        first = false;
        UINT support = 0;
        const HRESULT check = device->CheckFormatSupport(format.value, &support);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = 64;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = format.value;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (format.renderTarget ? D3D11_BIND_RENDER_TARGET : 0U);
        ComPtr<ID3D11Texture2D> texture;
        const HRESULT create = device->CreateTexture2D(&desc, nullptr, &texture);
        ComPtr<ID3D11ShaderResourceView> srv;
        const HRESULT view = SUCCEEDED(create) ? device->CreateShaderResourceView(texture.Get(), nullptr, &srv) : create;
        ComPtr<ID3D11RenderTargetView> rtv;
        const HRESULT renderView = SUCCEEDED(create) && format.renderTarget
            ? device->CreateRenderTargetView(texture.Get(), nullptr, &rtv) : S_FALSE;
        std::cout << "{\"format\":" << quoted(format.name) << ",\"supportHresult\":" << hr(check)
            << ",\"supportBits\":" << support << ",\"texture2D\":" << ((support & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0)
            << ",\"shaderLoad\":" << ((support & D3D11_FORMAT_SUPPORT_SHADER_LOAD) != 0)
            << ",\"shaderSample\":" << ((support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0)
            << ",\"renderTarget\":" << ((support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0)
            << ",\"blendable\":" << ((support & D3D11_FORMAT_SUPPORT_BLENDABLE) != 0)
            << ",\"createTextureHresult\":" << hr(create) << ",\"createSrvHresult\":" << hr(view)
            << ",\"createRtvHresult\":" << hr(renderView) << '}';
    }
    std::cout << ']';
}
static void timing(ID3D11Device* device, ID3D11DeviceContext* context) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 1024;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    const HRESULT create = device->CreateTexture2D(&desc, nullptr, &texture);
    ComPtr<ID3D11RenderTargetView> rtv;
    const HRESULT view = SUCCEEDED(create) ? device->CreateRenderTargetView(texture.Get(), nullptr, &rtv) : create;
    std::cout << "{\"textureHresult\":" << hr(create) << ",\"rtvHresult\":" << hr(view)
        << ",\"scope\":\"isolated-1024x1024-clear-upload-query-probe-not-game\"";
    if (FAILED(view)) { std::cout << '}'; return; }
    // An actual upload and readback validates the path without becoming part
    // of the game or claiming that a clear is a production rendering pass.
    std::vector<std::uint32_t> pixels(1024U * 1024U, 0xFF123456U);
    const auto uploadStart = Clock::now();
    context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), 4096, 0);
    const double uploadMs = milliseconds(Clock::now() - uploadStart);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    const HRESULT stage = device->CreateTexture2D(&desc, nullptr, &staging);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT mapResult = stage;
    bool exact = false;
    if (SUCCEEDED(stage)) {
        context->CopyResource(staging.Get(), texture.Get());
        mapResult = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(mapResult)) {
            exact = true;
            for (UINT row = 0; row < 1024 && exact; ++row) {
                const auto* values = reinterpret_cast<const std::uint32_t*>(
                    static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch);
                exact = std::all_of(values, values + 1024, [](std::uint32_t value) { return value == 0xFF123456U; });
            }
            context->Unmap(staging.Get(), 0);
        }
    }
    std::cout << ",\"uploadBytes\":4194304,\"uploadCpuCallMs\":" << uploadMs
        << ",\"readbackMapHresult\":" << hr(mapResult) << ",\"uploadReadbackExact\":" << exact;
    struct QuerySet {
        ComPtr<ID3D11Query> disjoint, start, end;
        HRESULT created = E_FAIL;
    };
    std::array<QuerySet, 8> queries;
    for (auto& query : queries) {
        D3D11_QUERY_DESC queryDesc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        query.created = device->CreateQuery(&queryDesc, &query.disjoint);
        queryDesc.Query = D3D11_QUERY_TIMESTAMP;
        if (SUCCEEDED(query.created)) query.created = device->CreateQuery(&queryDesc, &query.start);
        if (SUCCEEDED(query.created)) query.created = device->CreateQuery(&queryDesc, &query.end);
        if (FAILED(query.created)) continue;
        context->Begin(query.disjoint.Get());
        context->End(query.start.Get());
        for (int pass = 0; pass < 32; ++pass) {
            const float color[] = {static_cast<float>(pass) / 32.0F, 0.25F, 0.5F, 1.0F};
            context->ClearRenderTargetView(rtv.Get(), color);
        }
        context->End(query.end.Get());
        context->End(query.disjoint.Get());
    }
    context->Flush(); // Submit once; GetData below never flushes or substitutes CPU time.
    std::cout << ",\"asynchronousQueries\":[";
    for (std::size_t index = 0; index < queries.size(); ++index) {
        if (index) std::cout << ',';
        auto& query = queries[index];
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};
        UINT64 first = 0, last = 0;
        HRESULT ready = query.created;
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        int pendingPolls = 0;
        if (SUCCEEDED(ready)) {
            do {
                ready = context->GetData(query.disjoint.Get(), &data, sizeof(data), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if (ready == S_OK) ready = context->GetData(query.start.Get(), &first, sizeof(first), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if (ready == S_OK) ready = context->GetData(query.end.Get(), &last, sizeof(last), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if (ready == S_FALSE) { ++pendingPolls; Sleep(1); }
            } while (ready == S_FALSE && Clock::now() < deadline);
        }
        const bool valid = ready == S_OK && !data.Disjoint && data.Frequency != 0 && last >= first;
        std::cout << "{\"createHresult\":" << hr(query.created) << ",\"getDataHresult\":" << hr(ready)
            << ",\"pendingPolls\":" << pendingPolls << ",\"disjoint\":" << (data.Disjoint != FALSE)
            << ",\"frequency\":" << data.Frequency << ",\"start\":" << first << ",\"end\":" << last
            << ",\"valid\":" << valid << ",\"gpuElapsedMs\":";
        if (valid) std::cout << static_cast<double>(last - first) * 1000.0 / static_cast<double>(data.Frequency);
        else std::cout << "null";
        std::cout << '}';
    }
    std::cout << "]}";
}
static void deviceProbe(const char* label, D3D_DRIVER_TYPE driver) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL selected{};
    const HRESULT result = D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device, &selected, &context);
    std::cout << "{\"label\":" << quoted(label) << ",\"requestedDriverType\":" << static_cast<int>(driver)
        << ",\"createHresult\":" << hr(result) << ",\"featureLevelNumeric\":" << static_cast<unsigned int>(selected)
        << ",\"requestedFeatureLevels\":[49408,49152,45312,45056,41216,40960]"
        << ",\"countsAsHardwareGpu\":" << (SUCCEEDED(result) && driver == D3D_DRIVER_TYPE_HARDWARE);
    if (SUCCEEDED(result)) {
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> actual;
        ComPtr<IDXGIAdapter1> actual1;
        const HRESULT qi = device.As(&dxgi);
        const HRESULT get = SUCCEEDED(qi) ? dxgi->GetAdapter(&actual) : qi;
        const HRESULT get1 = SUCCEEDED(get) ? actual.As(&actual1) : get;
        std::cout << ",\"actualAdapterHresult\":" << hr(get1) << ",\"actualAdapter\":";
        if (SUCCEEDED(get1)) adapterInfo(actual1.Get()); else std::cout << "null";
        std::cout << ",\"creationFlags\":" << device->GetCreationFlags() << ",\"formats\":";
        formats(device.Get());
        std::cout << ",\"timing\":";
        timing(device.Get(), context.Get());
        context->ClearState();
        context->Flush();
    }
    std::cout << '}';
}
struct WindowsForPid { DWORD pid; std::vector<HWND> windows; };
static BOOL CALLBACK collectWindow(HWND window, LPARAM raw) {
    auto& info = *reinterpret_cast<WindowsForPid*>(raw);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == info.pid) info.windows.push_back(window);
    return TRUE;
}
static int observe(DWORD pid, int durationMs, bool topmostPolicy) {
    const bool dpiAware = SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid);
    if (process == nullptr) {
        std::cout << "{\"openProcessError\":" << GetLastError() << '}';
        return 1;
    }
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    std::cout << "{\"pid\":" << pid << ",\"intervalMs\":100,\"qpcFrequency\":" << frequency.QuadPart
        << ",\"topmostPolicy\":" << topmostPolicy << ",\"observerPhysicalDpiAware\":" << dpiAware
        << ",\"boundary\":\"desktop-compositor-and-target-visibility-not-app-scanout\",\"samples\":[";
    const auto start = Clock::now();
    bool first = true;
    while (WaitForSingleObject(process, 0) == WAIT_TIMEOUT && milliseconds(Clock::now() - start) < durationMs) {
        if (!first) std::cout << ',';
        first = false;
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        const bool memoryValid = GetProcessMemoryInfo(process,
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) != FALSE;
        std::cout << "{\"elapsedMs\":" << milliseconds(Clock::now() - start)
            << ",\"memoryValid\":" << memoryValid << ",\"workingSet\":" << memory.WorkingSetSize
            << ",\"peakWorkingSet\":" << memory.PeakWorkingSetSize << ",\"privateBytes\":" << memory.PrivateUsage
            << ",\"power\":";
        power();
        std::cout << ",\"dwm\":";
        dwm();
        WindowsForPid info{pid, {}};
        EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&info));
        std::cout << ",\"windows\":[";
        for (std::size_t index = 0; index < info.windows.size(); ++index) {
            if (index) std::cout << ',';
            const HWND window = info.windows[index];
            RECT client{}, frame{};
            GetClientRect(window, &client);
            GetWindowRect(window, &frame);
            // Only the process just spawned by this test is enumerated. Make
            // its benchmark window visible above ordinary apps, without
            // moving/resizing it or sending input to the user's other apps.
            const bool raise = topmostPolicy && IsWindowVisible(window) && !IsIconic(window)
                && client.right > client.left && client.bottom > client.top
                && (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0;
            const bool raised = !raise || SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
            DWORD foregroundPid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
            DWORD cloaked = 0;
            const HRESULT cloakResult = DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            int ownedPoints = 0;
            for (const auto fraction : {25, 50, 75}) {
                POINT point{client.left + (client.right - client.left) * fraction / 100,
                    client.top + (client.bottom - client.top) / 2};
                ClientToScreen(window, &point);
                DWORD pointPid = 0;
                GetWindowThreadProcessId(WindowFromPoint(point), &pointPid);
                ownedPoints += pointPid == pid;
            }
            std::array<wchar_t, 256> title{};
            GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
            std::cout << "{\"title\":" << quoted(utf8(title.data()))
                << ",\"visible\":" << (IsWindowVisible(window) != FALSE) << ",\"minimized\":" << (IsIconic(window) != FALSE)
                << ",\"foreground\":" << (foregroundPid == pid)
                << ",\"cloakHresult\":" << hr(cloakResult) << ",\"cloaked\":" << cloaked
                << ",\"raiseAttempted\":" << raise << ",\"raiseSucceeded\":" << raised
                << ",\"topmost\":" << ((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0)
                << ",\"clientWidth\":" << client.right - client.left << ",\"clientHeight\":" << client.bottom - client.top
                << ",\"left\":" << frame.left << ",\"top\":" << frame.top
                << ",\"right\":" << frame.right << ",\"bottom\":" << frame.bottom
                << ",\"threeClientPointsOwned\":" << ownedPoints << '}';
        }
        std::cout << "]}";
        Sleep(100);
    }
    CloseHandle(process);
    std::cout << "]}\n";
    return 0;
}
int main(int argc, char** argv) {
    std::cout << std::boolalpha << std::fixed << std::setprecision(6);
    if (argc == 4 && (std::string(argv[1]) == "--observe-pid" || std::string(argv[1]) == "--observe-visible-pid")) {
        const auto pid = std::strtoul(argv[2], nullptr, 10);
        const int duration = std::clamp(std::atoi(argv[3]), 100, 120000);
        if (pid == 0 || pid > MAXDWORD) return 2;
        return observe(static_cast<DWORD>(pid), duration, std::string(argv[1]) == "--observe-visible-pid");
    }
    if (argc != 1) return 2;
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    std::array<int, 4> registers{};
    std::array<char, 49> brand{};
    for (int leaf = 0; leaf < 3; ++leaf) {
        __cpuid(registers.data(), static_cast<int>(0x80000002U + static_cast<unsigned int>(leaf)));
        std::copy_n(reinterpret_cast<const char*>(registers.data()), 16, brand.data() + leaf * 16);
    }
    DWORD topologyBytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &topologyBytes);
    std::vector<std::uint64_t> topology((static_cast<std::size_t>(topologyBytes) + 7U) / 8U);
    const bool topologyValid = GetLogicalProcessorInformationEx(RelationProcessorCore,
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(topology.data()), &topologyBytes) != FALSE;
    std::cout << "{\"schema\":1,\"sessionId\":" << session << ",\"cpuBrand\":" << quoted(brand.data())
        << ",\"cpuTopologyValid\":" << topologyValid << ",\"processorCores\":[";
    if (topologyValid) {
        std::size_t offset = 0;
        bool first = true;
        while (offset < topologyBytes) {
            const auto* item = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
                reinterpret_cast<const std::uint8_t*>(topology.data()) + offset);
            if (item->Size == 0 || offset + item->Size > topologyBytes) break;
            if (!first) std::cout << ',';
            first = false;
            unsigned int logical = 0;
            for (WORD group = 0; group < item->Processor.GroupCount; ++group)
                logical += std::popcount(static_cast<unsigned long long>(item->Processor.GroupMask[group].Mask));
            std::cout << "{\"efficiencyClass\":" << static_cast<int>(item->Processor.EfficiencyClass)
                << ",\"logicalProcessors\":" << logical << '}';
            offset += item->Size;
        }
    }
    std::cout << "],\"power\":";
    power();
    std::cout << ",\"dwm\":";
    dwm();
    ComPtr<IDXGIFactory1> factory;
    const HRESULT create = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    std::cout << ",\"factoryHresult\":" << hr(create) << ",\"adapters\":[";
    if (SUCCEEDED(create)) {
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            const HRESULT result = factory->EnumAdapters1(index, &adapter);
            if (result == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(result)) { std::cout << "{\"enumerationHresult\":" << hr(result) << '}'; break; }
            if (index) std::cout << ',';
            adapterInfo(adapter.Get());
        }
    }
    std::cout << "],\"devices\":[";
    deviceProbe("default-hardware", D3D_DRIVER_TYPE_HARDWARE);
    std::cout << ',';
    deviceProbe("explicit-warp-software-control", D3D_DRIVER_TYPE_WARP);
    std::cout << "]}\n";
    return SUCCEEDED(create) ? 0 : 1;
}
