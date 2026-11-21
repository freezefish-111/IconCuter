# IconCuter

一款 Windows 桌面应用：导入 `.exe` 文件，自动提取其中的图标并保存为 `.ico` 文件。

## 使用

1. 运行 `.output/IconCuter.exe`
2. 点击「选择文件...」选取 `.exe`，或直接把 `.exe` 拖进窗口
3. 图标会自动导出到 `%USERPROFILE%\IcoCuter\Download`
   - 当前机器：`C:\Users\freezefish_Admin\IcoCuter\Download`
4. 点击「打开输出文件夹」可查看结果

## 说明

- 每个图标资源组导出为一个独立的 `.ico` 文件，文件名格式为 `<exe名>_<资源名>.ico`
- 优先直接读取 exe 内的 `RT_GROUP_ICON` / `RT_ICON` 资源，完整保留 16/24/32/48/256 像素等所有尺寸与原图质量
- 若 exe 没有标准资源组，则回退到 `ExtractIconEx` + `GetDIBits` 方式生成 `.ico`
- 同一个 exe 中同名资源自动追加 `_2`、`_3` 等后缀，避免覆盖

## 编译

```bat
build.bat
```

需要 MinGW-w64（g++）。输出位置：`.output/IconCuter.exe`。

源码：`src/main.cpp`（纯 Win32 API，无第三方依赖）。
