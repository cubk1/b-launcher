<img src="assets/images/logo.svg" width="100" alt="logo" />

## B Launcher™

轻量 Minecraft 启动器，使用纯C++开发

QQ交流群：858714745

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

## 构建与安装

```bash
pip install fonttools
scoop install upx

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --build build --config MinSizeRel

# For Linux distribution packages, use the shared libraries of the system and turn off the update check:
cmake -S . -B build -G Ninja -DBLB_SYSTEM_LIBS=ON -DBLB_UPDATE_CHECK=OFF

# For NixOS:
nix run github:cubk1/b-launcher
# or add the github:cubk1/b-launcher flake and put b-launcher.packages.${system}.default in your package list.
# The launcher finds Java in JAVA_HOME and PATH, for example environment.systemPackages = [ pkgs.jdk21 ].
# To give more JDKs to the launcher, use .override { jdks = [ pkgs.jdk8 pkgs.jdk21 ]; } on the package.
# The wrapper puts them in PATH of the launcher, before the PATH of the system.
# additionalLibs and additionalPrograms in .override add libraries and programs for the game.
# The launcher uses the Java version that the game needs.
# With programs.nix-ld.enable = true, the launcher can also download Java.
# Feature switches in .override:
#   textToSpeechSupport (default true on Linux): flite for the narrator of the game.
#   gamemodeSupport (default false): gamemoderun for the wrapper setting of an instance. It also
#     needs programs.gamemode.enable = true. For example .override { gamemodeSupport = true; }.

# For macOS (aarch64-darwin): the same flake builds bin/b-launcher and "Applications/B Launcher.app".
# The jdks and additionalPrograms of .override work as on NixOS. textToSpeechSupport and
# gamemodeSupport are for Linux only, and additionalLibs has no effect.
nix run github:cubk1/b-launcher
```

## Unix数据目录

Linux和macOS将配置、账户写入`$XDG_DATA_HOME/b-launcher/`，默认路径为`~/.local/share/b-launcher/`。`XDG_DATA_HOME`必须是绝对路径。首次读取时会迁移旧目录中的配置和账户。

## 截图

<img width="1184" height="818" alt="image" src="https://github.com/user-attachments/assets/d954a965-3077-4f3b-a2a2-ceae881594e0" />

<img width="1184" height="818" alt="image" src="https://github.com/user-attachments/assets/368fb4e8-c40a-4ccd-8919-33b48295600a" />

<img width="1184" height="818" alt="image" src="https://github.com/user-attachments/assets/74e8fcaf-2583-4b53-aa2b-a43833a86810" />

<img width="1184" height="818" alt="image" src="https://github.com/user-attachments/assets/9c26e2d6-4ae4-46e1-9e1b-d132b8ab9f48" />
