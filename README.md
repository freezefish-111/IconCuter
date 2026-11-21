# IconCuter

[中文](#中文) | [English](#english)

---

<a id="中文"></a>

## 中文

一款 Windows 桌面应用：导入 `.exe` 文件，自动提取其中的图标并保存为 `.ico` 文件。

### 使用

1. 运行 `.output/IconCuter.exe`
2. 点击「选择文件...」选取 `.exe`，或直接把 `.exe` 拖进窗口
3. 图标会自动导出到 `%USERPROFILE%\IcoCuter\Download`（即用户主目录下的 `IcoCuter\Download` 文件夹）
4. 点击「打开输出文件夹」可查看结果

### 说明

- 每个图标资源组导出为一个独立的 `.ico` 文件，文件名格式为 `<exe名>_<资源名>.ico`
- 优先直接读取 exe 内的 `RT_GROUP_ICON` / `RT_ICON` 资源，完整保留 16/24/32/48/256 像素等所有尺寸与原图质量
- 若 exe 没有标准资源组，则回退到 `ExtractIconEx` + `GetDIBits` 方式生成 `.ico`
- 同一个 exe 中同名资源自动追加 `_2`、`_3` 等后缀，避免覆盖

### 编译

```bat
build.bat
```

需要 MinGW-w64（g++）。输出位置：`.output/IconCuter.exe`。

源码：`src/main.cpp`（纯 Win32 API，无第三方依赖）。

Windows 版本已编译好，可在 [Releases](https://github.com/freezefish-111/IconCuter/releases) 页面直接下载使用。

---

<a id="english"></a>

## English

A Windows desktop app that imports an `.exe` file, extracts its icons automatically, and saves them as `.ico` files.

### Usage

1. Run `.output/IconCuter.exe`
2. Click **Select File...** to choose an `.exe`, or simply drag and drop the `.exe` onto the window
3. Icons are exported automatically to `%USERPROFILE%\IcoCuter\Download` (the `IcoCuter\Download` folder under your user profile)
4. Click **Open Output Folder** to view the results

### Details

- Each icon resource group is exported as a separate `.ico` file, named `<exe-name>_<resource-name>.ico`
- It first reads the `RT_GROUP_ICON` / `RT_ICON` resources directly from the exe, preserving all sizes (16/24/32/48/256 px) and original image quality
- If the exe has no standard resource group, it falls back to `ExtractIconEx` + `GetDIBits` to generate the `.ico`
- Duplicate resource names from the same exe get `_2`, `_3`, ... suffixes to avoid overwriting

### Build

```bat
build.bat
```

Requires MinGW-w64 (g++). Output: `.output/IconCuter.exe`.

Source: `src/main.cpp` (pure Win32 API, no third-party dependencies).

A prebuilt Windows binary is available on the [Releases](https://github.com/freezefish-111/IconCuter/releases) page.
