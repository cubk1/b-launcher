<img src="assets/images/logo.svg" width="100" alt="logo" />

## B Launcher™

轻量 Minecraft 启动器，使用纯C++开发

QQ交流群：858714745

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
# additionalLibs and additionalPrograms in .override add libraries and programs for the game.
# The launcher uses the Java version that the game needs. For the same version, it uses jdks first,
# then JAVA_HOME, then PATH.
# With programs.nix-ld.enable = true, the launcher can also download Java.
# Feature switches in .override:
#   textToSpeechSupport (default true): flite for the narrator of the game.
#   gamemodeSupport (default false): gamemoderun for the wrapper setting of an instance. It also
#     needs programs.gamemode.enable = true. For example .override { gamemodeSupport = true; }.
```

## Unix数据目录

Linux和macOS将配置、账户写入`$XDG_DATA_HOME/b-launcher/`，默认路径为`~/.local/share/b-launcher/`。`XDG_DATA_HOME`必须是绝对路径。首次读取时会迁移旧目录中的配置和账户。