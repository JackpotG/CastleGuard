# 微信小游戏转换工具

## 版本: v1.0.35

## 使用方式

### 方式1: 使用配置文件（推荐）

1. 创建配置文件 config.json:
{
  "jsFile": "build/game.js",
  "wasmFile": "build/game.wasm",
  "dataFile": "build/game.data",
  "cdnUrl": "https://your-cdn.com/",
  "appID": "wx1234567890",
  "appName": "我的游戏",
  "engineName": "Unity",
  "engineVersion": "2021.3.0",
  "webglVersion": 2,
  "deviceOrientation": "landscape"
}

2. 运行转换工具:
   Windows: .\\wx-transformer-win.exe --config config.json
   macOS:   ./wx-transformer-mac --config config.json
   Linux:   ./wx-transformer-linux --config config.json

### 方式2: 使用命令行参数

#### Windows 系统
.\\wx-transformer-win.exe -j game.js -w game.wasm -d game.data -c http://localhost:8000/

#### macOS 系统
chmod +x wx-transformer-mac
./wx-transformer-mac -j game.js -w game.wasm -d game.data -c http://localhost:8000/

#### Linux 系统
chmod +x wx-transformer-linux
./wx-transformer-linux -j game.js -w game.wasm -d game.data -c http://localhost:8000/

## 参数说明

### 必需参数
- -j, --js-file: JavaScript 胶水代码文件路径
- -w, --wasm-file: WebAssembly 文件路径

### 可选参数
- -d, --data-file: 数据包文件路径
- -dj, --data-js-file: 数据JS文件路径 (UE引擎需要)
- -c, --cdn-url: 游戏资源CDN地址 (默认: http://localhost:8000/)
- -o, --output: 导出路径 (默认: wechat-build)
- --config: 使用配置文件

### 基本信息配置
- --app-id: 游戏AppID
- --app-name: 小游戏项目名
- --engine-name: 引擎名称 (如: Unity, Unreal)
- --engine-version: 引擎版本号 (如: 2021.3.0)

### 运行时配置
- --webgl-version: WebGL版本 (1或2)
- --alpha-channel: 透明度通道 (true或false)
- --device-orientation: 游戏方向 (portrait/landscape)

### 资源加载配置
- --background-video: 加载阶段视频URL
- --load-from-subpackage: 首包资源从本地加载
- --no-compress: 不压缩首包资源
- --force-single-thread: 强制使用单线程压缩
- --preload-list: 预下载文件列表 (JSON格式)

## 多线程压缩说明

工具会自动检测并使用平台特定的原生 Brotli 多线程压缩模块。
原生模块已内置于可执行文件中，支持以下平台：
- Windows x64
- macOS Apple Silicon (arm64)
- macOS Intel (x64)
- Linux x64

如果未找到匹配平台的原生模块，工具会打印平台名称并自动降级到单线程压缩模式。

## 配置文件示例

完整配置示例请参考 config.json 文件

## 更多帮助

运行以下命令查看完整帮助信息:
  Windows: .\\wx-transformer-win.exe --help
  macOS:   ./wx-transformer-mac --help
  Linux:   ./wx-transformer-linux --help