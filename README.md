<img src="assets/images/logo.svg" width="100" alt="logo" />

## B Launcher™

轻量 Minecraft 启动器，使用纯C++开发

## 优势

- 使用纯C++开发，原生UI，没有Java、Electron、Tauri、.NET
- 低内存占用，对垃圾电脑友好
- 有一个自己的Client ID，不使用Prism Launcher的
- 自动迁移账户从PCL和HMCL和官启
- 没有云控，混淆，VMProtect，.NET Reactor
- 所有代码全部开源，且有一个宽松的开源协议。
- 有一个主题取色器，可以自定义主题，而且你不用为此支付任何钱。
- 支持1.19+的自定义皮肤、披风
- 修复1.7、1.8版本Minecraft中文输入
- 由提示词水平和审美都健在的开发者维护

## 构建

```bash
pip install fonttools
scoop install upx

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --build build --config MinSizeRel
```

## 截图

<img width="1184" height="818" alt="image" src="https://github.com/user-attachments/assets/d954a965-3077-4f3b-a2a2-ceae881594e0" />

<img width="1184" height="818" alt="image" src="https://github.com/user-attachments/assets/368fb4e8-c40a-4ccd-8919-33b48295600a" />

<img width="1184" height="818" alt="image" src="https://github.com/user-attachments/assets/74e8fcaf-2583-4b53-aa2b-a43833a86810" />

<img width="1184" height="818" alt="image" src="https://github.com/user-attachments/assets/9c26e2d6-4ae4-46e1-9e1b-d132b8ab9f48" />
