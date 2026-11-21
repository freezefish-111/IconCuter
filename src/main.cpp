// IconCuter - 从 .exe 中提取图标并保存为 .ico 文件
// 纯 Win32 API 实现，无第三方依赖

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

// ---------- 常量 ----------
#define ID_SELECT       1001
#define ID_PATH         1002
#define ID_EXTRACT      1003
#define ID_LIST         1004
#define ID_STATUS       1005
#define ID_OPEN_FOLDER  1006

#define WM_USER_EXTRACT (WM_USER + 1)

// ---------- ICO / 资源结构（必须按字节对齐） ----------
#pragma pack(push, 1)
struct IconDir {
    WORD reserved;   // 0
    WORD type;       // 1 = icon
    WORD count;
};

struct IconDirEntry {
    BYTE  width;     // 0 表示 256
    BYTE  height;
    BYTE  colorCount;
    BYTE  reserved;
    WORD  planes;
    WORD  bitCount;
    DWORD bytesInRes;
    DWORD imageOffset;
};

struct GrpIconDirEntry {
    BYTE  width;
    BYTE  height;
    BYTE  colorCount;
    BYTE  reserved;
    WORD  planes;
    WORD  bitCount;
    DWORD bytesInRes;
    WORD  id;
};

struct BitmapInfoHeader {
    DWORD size;
    LONG  width;
    LONG  height;
    WORD  planes;
    WORD  bitCount;
    DWORD compression;
    DWORD sizeImage;
    LONG  xPixelsPerMeter;
    LONG  yPixelsPerMeter;
    DWORD colorsUsed;
    DWORD colorsImportant;
};
#pragma pack(pop)

struct IconImage {
    IconDirEntry entry;
    std::vector<BYTE> data;
};

struct GroupContext {
    HMODULE module;
    std::wstring outputDir;
    std::wstring stem;
    std::set<std::wstring> usedNames;
    std::vector<std::wstring> savedFiles;
};

// ---------- 通用工具 ----------

static std::wstring GetProfileDir() {
    WCHAR buf[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        n = GetWindowsDirectoryW(buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return L"C:\\";
    }
    return std::wstring(buf, n);
}

static std::wstring GetOutputDir() {
    std::wstring profile = GetProfileDir();
    while (!profile.empty() && (profile.back() == L'\\' || profile.back() == L'/'))
        profile.pop_back();
    std::wstring parent = profile + L"\\IcoCuter";
    std::wstring out = parent + L"\\Download";
    CreateDirectoryW(parent.c_str(), nullptr);
    CreateDirectoryW(out.c_str(), nullptr);
    return out;
}

static std::wstring GetFileStem(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);
    if (name.empty()) name = L"icon";
    return name;
}

static bool HasExeExtension(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext == L"exe";
}

static std::wstring SanitizeName(const std::wstring& name) {
    std::wstring out;
    for (WCHAR ch : name) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' ||
            ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
            out.push_back(L'_');
        else
            out.push_back(ch);
    }
    if (out.empty()) out = L"icon";
    return out;
}

static std::wstring UniquePath(const std::wstring& dir, const std::wstring& base,
                               std::set<std::wstring>& used) {
    std::wstring name = base;
    int suffix = 2;
    while (used.count(name)) {
        name = base + L"_" + std::to_wstring(suffix++);
    }
    used.insert(name);
    return dir + L"\\" + name + L".ico";
}

static bool WriteIcoFile(const std::wstring& path, const std::vector<IconImage>& images) {
    if (images.empty()) return false;

    IconDir dir;
    dir.reserved = 0;
    dir.type = 1;
    dir.count = static_cast<WORD>(images.size());

    std::vector<BYTE> file;
    file.resize(sizeof(IconDir) + sizeof(IconDirEntry) * images.size());
    std::memcpy(file.data(), &dir, sizeof(dir));

    DWORD offset = static_cast<DWORD>(file.size());
    for (size_t i = 0; i < images.size(); ++i) {
        IconDirEntry e = images[i].entry;
        e.imageOffset = offset;
        std::memcpy(file.data() + sizeof(IconDir) + i * sizeof(IconDirEntry),
                    &e, sizeof(e));
        offset += static_cast<DWORD>(images[i].data.size());
    }
    for (const IconImage& img : images)
        file.insert(file.end(), img.data.begin(), img.data.end());

    std::ofstream ofs(path.c_str(), std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs.write(reinterpret_cast<const char*>(file.data()), file.size());
    return ofs.good();
}

static std::wstring FileSizeText(size_t bytes) {
    WCHAR buf[64];
    if (bytes < 1024)
        swprintf_s(buf, 64, L"%zu B", bytes);
    else
        swprintf_s(buf, 64, L"%.1f KB", bytes / 1024.0);
    return buf;
}

// ---------- 方法一：直接解析 RT_GROUP_ICON / RT_ICON 资源（保留原始质量，含 256x256） ----------

static BOOL CALLBACK EnumGroupProc(HMODULE hMod, LPCWSTR /*lpType*/,
                                   LPWSTR lpName, LONG_PTR lParam) {
    GroupContext* ctx = reinterpret_cast<GroupContext*>(lParam);

    HRSRC hGroup = FindResourceW(hMod, lpName, RT_GROUP_ICON);
    if (!hGroup) return TRUE;
    DWORD groupSize = SizeofResource(hMod, hGroup);
    HGLOBAL hGroupMem = LoadResource(hMod, hGroup);
    if (!hGroupMem || groupSize < 6) return TRUE;
    const BYTE* groupData = static_cast<const BYTE*>(LockResource(hGroupMem));
    if (!groupData) return TRUE;

    IconDir dir;
    std::memcpy(&dir, groupData, sizeof(dir));
    if (dir.reserved != 0 || dir.type != 1 || dir.count == 0) return TRUE;

    std::wstring groupName;
    if (IS_INTRESOURCE(lpName))
        groupName = L"ID_" + std::to_wstring(static_cast<WORD>(reinterpret_cast<ULONG_PTR>(lpName) & 0xFFFF));
    else
        groupName = lpName;

    std::vector<IconImage> images;
    for (WORD i = 0; i < dir.count; ++i) {
        size_t entryPos = sizeof(IconDir) + static_cast<size_t>(i) * sizeof(GrpIconDirEntry);
        if (entryPos + sizeof(GrpIconDirEntry) > groupSize) break;

        GrpIconDirEntry src;
        std::memcpy(&src, groupData + entryPos, sizeof(src));

        HRSRC hIcon = FindResourceW(hMod, MAKEINTRESOURCEW(src.id), RT_ICON);
        if (!hIcon) continue;
        DWORD imgSize = SizeofResource(hMod, hIcon);
        HGLOBAL hIconMem = LoadResource(hMod, hIcon);
        if (!hIconMem || imgSize == 0) continue;
        const BYTE* imgData = static_cast<const BYTE*>(LockResource(hIconMem));
        if (!imgData) continue;

        IconImage img;
        std::memset(&img.entry, 0, sizeof(img.entry));
        img.entry.width = src.width;
        img.entry.height = src.height;
        img.entry.colorCount = src.colorCount;
        img.entry.reserved = src.reserved;
        img.entry.planes = src.planes;
        img.entry.bitCount = src.bitCount;
        img.entry.bytesInRes = imgSize;
        img.entry.imageOffset = 0;
        img.data.assign(imgData, imgData + imgSize);
        images.push_back(std::move(img));
    }

    if (images.empty()) return TRUE;

    std::wstring baseName = ctx->stem + L"_" + SanitizeName(groupName);
    std::wstring outPath = UniquePath(ctx->outputDir, baseName, ctx->usedNames);
    if (WriteIcoFile(outPath, images))
        ctx->savedFiles.push_back(outPath);
    return TRUE;
}

static size_t ExtractFromResources(const std::wstring& exePath,
                                   const std::wstring& outputDir,
                                   const std::wstring& stem,
                                   std::set<std::wstring>& usedNames,
                                   std::vector<std::wstring>& savedFiles) {
    HMODULE hMod = LoadLibraryExW(exePath.c_str(), nullptr,
                                  LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!hMod)
        hMod = LoadLibraryExW(exePath.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!hMod) return 0;

    GroupContext ctx;
    ctx.module = hMod;
    ctx.outputDir = outputDir;
    ctx.stem = stem;
    ctx.usedNames = usedNames;
    EnumResourceNamesW(hMod, RT_GROUP_ICON, EnumGroupProc,
                       reinterpret_cast<LONG_PTR>(&ctx));
    FreeLibrary(hMod);

    usedNames = std::move(ctx.usedNames);
    savedFiles.insert(savedFiles.end(), ctx.savedFiles.begin(), ctx.savedFiles.end());
    return ctx.savedFiles.size();
}

// ---------- 方法二：通过 ExtractIconEx + GetDIBits 转换（兼容无标准资源组的 exe） ----------

static bool IconToImage(HICON hicon, IconImage& out) {
    ICONINFO ii;
    if (!GetIconInfo(hicon, &ii)) return false;

    BITMAP bm;
    int w = 0, h = 0;
    bool monochrome = (ii.hbmColor == nullptr);
    if (ii.hbmColor) {
        if (GetObject(ii.hbmColor, sizeof(bm), &bm) == sizeof(bm)) {
            w = bm.bmWidth;
            h = bm.bmHeight;
        }
    } else if (ii.hbmMask) {
        if (GetObject(ii.hbmMask, sizeof(bm), &bm) == sizeof(bm)) {
            w = bm.bmWidth;
            h = bm.bmHeight / 2;
        }
    }
    if (w <= 0 || h <= 0 || w > 256 || h > 256) {
        DeleteObject(ii.hbmColor);
        DeleteObject(ii.hbmMask);
        return false;
    }

    HDC hdc = GetDC(nullptr);
    std::vector<BYTE> color(static_cast<size_t>(w) * h * 4, 0);
    int maskStride = ((w + 31) / 32) * 4;
    std::vector<BYTE> mask(static_cast<size_t>(maskStride) * h, 0);

    BITMAPINFO bi;
    std::memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    if (!monochrome) {
        GetDIBits(hdc, ii.hbmColor, 0, h, color.data(), &bi, DIB_RGB_COLORS);
        // 某些驱动不返回 alpha：若全为 0，则全部设为 255（透明度交由 AND 掩码处理）
        bool allZeroAlpha = true;
        for (int i = 3; i < static_cast<int>(color.size()); i += 4) {
            if (color[i] != 0) { allZeroAlpha = false; break; }
        }
        if (allZeroAlpha) {
            for (int i = 3; i < static_cast<int>(color.size()); i += 4)
                color[i] = 0xFF;
        }
    } else {
        int monoStride = maskStride;
        std::vector<BYTE> mono(static_cast<size_t>(monoStride) * h * 2, 0);
        BITMAPINFO bi1;
        std::memset(&bi1, 0, sizeof(bi1));
        bi1.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi1.bmiHeader.biWidth = w;
        bi1.bmiHeader.biHeight = h * 2;
        bi1.bmiHeader.biPlanes = 1;
        bi1.bmiHeader.biBitCount = 1;
        bi1.bmiHeader.biCompression = BI_RGB;
        GetDIBits(hdc, ii.hbmMask, 0, h * 2, mono.data(), &bi1, DIB_RGB_COLORS);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                BYTE bit = (mono[static_cast<size_t>(y) * monoStride + x / 8] >> (7 - (x % 8))) & 1;
                DWORD pixel = bit ? 0xFFFFFFFFu : 0xFF000000u;
                std::memcpy(&color[(static_cast<size_t>(y) * w + x) * 4], &pixel, 4);
            }
        }
        std::memcpy(mask.data(), mono.data() + static_cast<size_t>(monoStride) * h, mask.size());
    }

    if (!monochrome && ii.hbmMask) {
        BITMAPINFO biMask;
        std::memset(&biMask, 0, sizeof(biMask));
        biMask.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        biMask.bmiHeader.biWidth = w;
        biMask.bmiHeader.biHeight = h;
        biMask.bmiHeader.biPlanes = 1;
        biMask.bmiHeader.biBitCount = 1;
        biMask.bmiHeader.biCompression = BI_RGB;
        GetDIBits(hdc, ii.hbmMask, 0, h, mask.data(), &biMask, DIB_RGB_COLORS);
    }
    ReleaseDC(nullptr, hdc);

    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);

    BitmapInfoHeader header;
    std::memset(&header, 0, sizeof(header));
    header.size = sizeof(header);
    header.width = w;
    header.height = h * 2;   // 颜色位图 + AND 掩码
    header.planes = 1;
    header.bitCount = 32;
    header.compression = BI_RGB;
    header.sizeImage = static_cast<DWORD>(color.size() + mask.size());

    out.data.resize(sizeof(header) + color.size() + mask.size());
    std::memcpy(out.data.data(), &header, sizeof(header));
    std::memcpy(out.data.data() + sizeof(header), color.data(), color.size());
    std::memcpy(out.data.data() + sizeof(header) + color.size(), mask.data(), mask.size());

    std::memset(&out.entry, 0, sizeof(out.entry));
    out.entry.width = (w == 256) ? 0 : static_cast<BYTE>(w);
    out.entry.height = (h == 256) ? 0 : static_cast<BYTE>(h);
    out.entry.planes = 1;
    out.entry.bitCount = 32;
    out.entry.bytesInRes = static_cast<DWORD>(out.data.size());
    return true;
}

static size_t ExtractFallback(const std::wstring& exePath,
                              const std::wstring& outputDir,
                              const std::wstring& stem,
                              std::set<std::wstring>& usedNames,
                              std::vector<std::wstring>& savedFiles) {
    UINT total = ExtractIconExW(exePath.c_str(), -1, nullptr, nullptr, 0);
    for (UINT i = 0; i < total; ++i) {
        HICON large = nullptr, small = nullptr;
        UINT n = ExtractIconExW(exePath.c_str(), i, &large, &small, 1);
        if (n == 0) continue;

        std::vector<IconImage> images;
        IconImage img;
        if (large && IconToImage(large, img)) images.push_back(std::move(img));
        if (small && IconToImage(small, img)) images.push_back(std::move(img));
        DestroyIcon(large);
        DestroyIcon(small);

        if (images.empty()) continue;
        std::wstring outPath = UniquePath(outputDir, stem + L"_" + std::to_wstring(i + 1),
                                          usedNames);
        if (WriteIcoFile(outPath, images))
            savedFiles.push_back(outPath);
    }
    return savedFiles.size();
}

// ---------- GUI ----------

static HWND g_hPathEdit, g_hList, g_hStatus;
static std::wstring g_outputDir;

static HFONT CreateUiFont() {
    return CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

static void SetStatus(const std::wstring& text) {
    SetWindowTextW(g_hStatus, text.c_str());
}

static void AddListLine(const std::wstring& text) {
    SendMessageW(g_hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

static void RunExtract(const std::wstring& exePath) {
    if (exePath.empty()) {
        SetStatus(L"请先选择一个 .exe 文件。");
        return;
    }
    if (!HasExeExtension(exePath)) {
        SetStatus(L"不支持的文件类型，请选择 .exe 文件。");
        return;
    }

    g_outputDir = GetOutputDir();
    std::wstring stem = GetFileStem(exePath);

    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    SetStatus(L"正在提取图标...");

    std::set<std::wstring> usedNames;
    std::vector<std::wstring> savedFiles;

    ExtractFromResources(exePath, g_outputDir, stem, usedNames, savedFiles);
    if (savedFiles.empty())
        ExtractFallback(exePath, g_outputDir, stem, usedNames, savedFiles);

    for (const std::wstring& file : savedFiles) {
        WIN32_FILE_ATTRIBUTE_DATA attr;
        size_t bytes = 0;
        if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &attr))
            bytes = static_cast<size_t>(attr.nFileSizeLow);
        size_t slash = file.find_last_of(L'\\');
        std::wstring name = (slash == std::wstring::npos) ? file : file.substr(slash + 1);
        AddListLine(name + L"   (" + FileSizeText(bytes) + L")");
    }

    if (savedFiles.empty())
        SetStatus(L"未在该文件中找到可提取的图标资源。");
    else {
        std::wstring msg = L"完成：已导出 " + std::to_wstring(savedFiles.size()) +
                           L" 个图标文件到 " + g_outputDir;
        SetStatus(msg);
    }
}

static void SelectFile(HWND hwnd) {
    WCHAR file[MAX_PATH * 4] = {0};
    OPENFILENAMEW ofn;
    std::memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = L"选择要提取图标的 EXE 文件";
    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(g_hPathEdit, file);
        RunExtract(file);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT font = CreateUiFont();
        CreateWindowW(L"static", L"EXE 文件：", WS_CHILD | WS_VISIBLE,
                      20, 20, 80, 22, hwnd, nullptr, nullptr, nullptr);
        g_hPathEdit = CreateWindowW(L"edit", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
                                    ES_AUTOHSCROLL | ES_READONLY,
                                    100, 18, 420, 24, hwnd,
                                    reinterpret_cast<HMENU>(ID_PATH), nullptr, nullptr);
        CreateWindowW(L"button", L"选择文件...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      535, 17, 100, 26, hwnd,
                      reinterpret_cast<HMENU>(ID_SELECT), nullptr, nullptr);

        CreateWindowW(L"button", L"提取图标", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                      20, 58, 110, 30, hwnd,
                      reinterpret_cast<HMENU>(ID_EXTRACT), nullptr, nullptr);
        CreateWindowW(L"button", L"打开输出文件夹", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      145, 58, 140, 30, hwnd,
                      reinterpret_cast<HMENU>(ID_OPEN_FOLDER), nullptr, nullptr);

        CreateWindowW(L"static", L"已提取的图标：", WS_CHILD | WS_VISIBLE,
                      20, 102, 160, 20, hwnd, nullptr, nullptr, nullptr);
        g_hList = CreateWindowW(L"listbox", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
                                WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
                                20, 124, 615, 200, hwnd,
                                reinterpret_cast<HMENU>(ID_LIST), nullptr, nullptr);

        g_hStatus = CreateWindowW(L"static", L"就绪。请选择 .exe 文件，或直接把文件拖进窗口。",
                                  WS_CHILD | WS_VISIBLE | SS_LEFT,
                                  20, 336, 615, 22, hwnd,
                                  reinterpret_cast<HMENU>(ID_STATUS), nullptr, nullptr);

        EnumChildWindows(hwnd, [](HWND h, LPARAM p) -> BOOL {
            SendMessageW(h, WM_SETFONT, static_cast<WPARAM>(p), TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(font));
        DragAcceptFiles(hwnd, TRUE);
        g_outputDir = GetOutputDir();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_SELECT:
            SelectFile(hwnd);
            return 0;
        case ID_EXTRACT: {
            WCHAR path[MAX_PATH * 4] = {0};
            GetWindowTextW(g_hPathEdit, path, MAX_PATH * 4);
            RunExtract(path);
            return 0;
        }
        case ID_OPEN_FOLDER:
            ShellExecuteW(nullptr, L"open", g_outputDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        if (count > 0) {
            WCHAR path[MAX_PATH * 4] = {0};
            DragQueryFileW(drop, 0, path, MAX_PATH * 4);
            SetWindowTextW(g_hPathEdit, path);
            RunExtract(path);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    const WCHAR* className = L"IconCuterWindowClass";
    WNDCLASSW wc;
    std::memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;

    if (!RegisterClassW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, className, L"IconCuter - EXE 图标提取器",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 670, 400,
                                nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
