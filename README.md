<img src="assets/images/logo.svg" align="left" width="64" height="64" alt="logo" />

<h3>B Launcher™</h3>
<p>轻量 Minecraft 启动器，使用纯C++开发</p>

<br clear="left" />

## 优势

- 使用纯C++开发，原生UI，没有Java、Electron、Tauri、.NET
- 低内存占用，对垃圾电脑友好
- 有一个自己的Client ID，不使用Prism Launcher的
- 自动迁移账户从PCL和HMCL和官启
- 没有云控，混淆，VMProtect，.NET Reactor
- 所有代码全部开源，且有一个宽松的开源协议。
- 有一个主题取色器，可以自定义主题，而且你不用为此支付任何钱。
- 由提示词水平和审美都健在的开发者维护

## 构建

```bash
pip install fonttools
scoop install upx

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --build build --config MinSizeRel
```