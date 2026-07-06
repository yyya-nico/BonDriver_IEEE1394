# BonDriver_IEEE1394 ビルドガイド

## プロジェクト構成

```
BonDriver_IEEE1394/
├── BonDriver_IEEE1394.sln          # Visual Studioソリューションファイル
├── BonDriver_IEEE1394.vcxproj      # BonDriver DLLプロジェクト
├── TestApp.vcxproj                 # テストアプリケーションプロジェクト
├── IBonDriver.h                    # BonDriverインターフェース定義
├── BonDriver_IEEE1394.h            # BonDriver実装ヘッダー
├── BonDriver_IEEE1394.cpp          # BonDriver実装
├── BonDriver_IEEE1394.def          # DLLエクスポート定義
├── TestApp.cpp                     # テストアプリケーション
├── BonDriver_IEEE1394.ini          # 設定ファイル（サンプル）
├── README.md                       # プロジェクト概要
├── USAGE.md                        # 使用ガイド
├── .gitignore                      # Git除外設定
└── bin/                            # ビルド出力先
    ├── Win32/
    │   ├── Debug/
    │   └── Release/
    └── x64/
        ├── Debug/
        └── Release/
```

## 必要な環境

### 開発環境

- **Visual Studio**: 2019以降（2022推奨）
- **Windows SDK**: 10.0以降
- **C++コンパイラ**: C++17対応

### ランタイム環境

- **OS**: Windows 10/11 (x64推奨)
- **VC++ 再頒布可能パッケージ**: Visual Studio 2019/2022
- **IEEE 1394ポート**: FireWire 400/800対応ポート

## ビルド手順

### 1. Visual Studioでビルド

#### GUIから

1. `BonDriver_IEEE1394.sln`をVisual Studioで開く
2. ビルド構成を選択
   - Debug/Release
   - Win32/x64
3. メニュー → ビルド → ソリューションのビルド
4. `bin\[Platform]\[Configuration]\`に出力される

#### コマンドラインから

```powershell
# Releaseビルド (x64)
msbuild BonDriver_IEEE1394.sln /p:Configuration=Release /p:Platform=x64

# Debugビルド (x64)
msbuild BonDriver_IEEE1394.sln /p:Configuration=Debug /p:Platform=x64

# Releaseビルド (Win32)
msbuild BonDriver_IEEE1394.sln /p:Configuration=Release /p:Platform=Win32

# すべてのビルド構成
msbuild BonDriver_IEEE1394.sln /p:Configuration=Release /p:Platform="x64;Win32"
```

### 2. 出力ファイル

#### BonDriver DLL

- **ファイル名**: `BonDriver_IEEE1394.dll`
- **場所**: `bin\[Platform]\[Configuration]\`
- **用途**: BonDriver本体（TVTestなどで使用）

#### テストアプリケーション

- **ファイル名**: `TestApp.exe`
- **場所**: `bin\[Platform]\[Configuration]\`
- **用途**: BonDriverの動作確認

#### その他のファイル

- `BonDriver_IEEE1394.lib`: インポートライブラリ
- `BonDriver_IEEE1394.pdb`: デバッグシンボル
- `TestApp.pdb`: デバッグシンボル

## ビルドオプション

### プリプロセッサ定義

#### BonDriver_IEEE1394.vcxproj

- **Debug**:
  - `_DEBUG`
  - `BONDRIENIEEE1394_EXPORTS`
  - `_WINDOWS`
  - `_USRDLL`

- **Release**:
  - `NDEBUG`
  - `BONDRIENIEEE1394_EXPORTS`
  - `_WINDOWS`
  - `_USRDLL`

#### TestApp.vcxproj

- **Debug**: `_DEBUG`, `_CONSOLE`
- **Release**: `NDEBUG`, `_CONSOLE`

### コンパイラオプション

- `/std:c++17`: C++17標準
- `/permissive-`: 標準準拠モード
- `/W3`: 警告レベル3
- `/O2`: 最適化（Release）
- `/Zi`: デバッグ情報生成

### リンカーオプション

#### 必須ライブラリ

- `strmiids.lib`: DirectShow
- `quartz.lib`: DirectShow
- `ole32.lib`: COM
- `oleaut32.lib`: OLE Automation

#### DLLエクスポート

- `/DEF:BonDriver_IEEE1394.def`: エクスポート定義ファイル

## トラブルシューティング

### ビルドエラー

#### エラー: qedit.h が見つからない

**原因**: 古いWindows SDKまたはDirectShow SDK

**解決策**: 
- プロジェクトは修正済み（`qedit.h`を使用しない実装）
- 最新版をビルドしてください

#### エラー: ED_MODE_* が定義されていない

**原因**: strmif.hの定義が不足

**解決策**:
- プロジェクトは修正済み（直接数値を使用）
- 最新版をビルドしてください

#### エラー: strmiids.lib が見つからない

**原因**: Windows SDKが正しくインストールされていない

**解決策**:
1. Visual Studio Installerを開く
2. 「Windows 10 SDK」をインストール
3. Visual Studioを再起動

### 実行時エラー

#### エラー: MSVCP140.dll が見つからない

**原因**: VC++再頒布可能パッケージ未インストール

**解決策**:
- [Microsoft Visual C++ 再頒布可能パッケージ](https://aka.ms/vs/17/release/vc_redist.x64.exe)をインストール

#### エラー: IEEE 1394デバイスを開けない

**原因**: デバイス未接続またはドライバー未インストール

**解決策**:
1. デバイスマネージャーでデバイス確認
2. IEEE 1394ドライバーを再インストール
3. デバイスの電源を確認

## カスタマイズ

### デバイス列挙の変更

`BonDriver_IEEE1394.cpp`の`EnumerateIEEE1394Devices()`メソッドを修正：

```cpp
// 特定のデバイス名で絞り込み
if (wcsstr(name, L"Sony") && wcsstr(name, L"DV"))
{
    // Sonyかつ"DV"を含むデバイスのみ
}
```

### バッファサイズの変更

`BonDriver_IEEE1394.h`で定義変更：

```cpp
static const DWORD MAX_QUEUE_SIZE = 512; // デフォルト: 256
```

### タイムアウト時間の変更

`TestApp.cpp`や呼び出し側で調整：

```cpp
DWORD result = pDriver->WaitTsStream(5000); // 5秒タイムアウト
```

## デバッグ

### Visual Studioデバッガー

1. TestAppプロジェクトをスタートアッププロジェクトに設定
2. ブレークポイントを設定
3. F5でデバッグ開始

### ログ出力

デバッグ用にログ出力を追加：

```cpp
#ifdef _DEBUG
    OutputDebugStringW(L"デバッグメッセージ\n");
#endif
```

### DirectShow GraphEdit

DirectShowグラフを視覚的に確認：

1. GraphEdit.exe を起動（Windows SDKに含まれる）
2. Graph → Insert Filters
3. IEEE 1394デバイスを追加
4. フィルター接続を確認

## パフォーマンス最適化

### Releaseビルドの最適化

すでに以下の最適化が有効：
- `/O2`: 速度優先の最適化
- `/GL`: プログラム全体の最適化
- `/LTCG`: リンク時コード生成

### さらなる最適化

1. **プロファイルガイド最適化（PGO）**
   - プロジェクト設定 → C/C++ → 最適化 → プロファイルガイド付き最適化

2. **CPU命令セット**
   - プロジェクト設定 → C/C++ → コード生成 → 拡張命令セット

## 配布

### 最小配布ファイル

```
配布フォルダ/
├── BonDriver_IEEE1394.dll
├── BonDriver_IEEE1394.ini (オプション)
└── README.txt
```

### インストーラー作成

WiXやInno Setupを使用してインストーラーを作成可能：

```xml
<!-- WiX例 -->
<Component Id="BonDriver" Guid="...">
  <File Id="BonDriverDLL" Source="BonDriver_IEEE1394.dll" KeyPath="yes"/>
</Component>
```

## 継続的インテグレーション（CI）

### GitHub Actions例

```yaml
name: Build

on: [push, pull_request]

jobs:
  build:
    runs-on: windows-latest
    steps:
    - uses: actions/checkout@v3
    - uses: microsoft/setup-msbuild@v1
    - name: Build
      run: msbuild BonDriver_IEEE1394.sln /p:Configuration=Release /p:Platform=x64
    - name: Upload artifacts
      uses: actions/upload-artifact@v3
      with:
        name: BonDriver_IEEE1394
        path: bin/x64/Release/BonDriver_IEEE1394.dll
```

## ライセンス

MIT License - 詳細はプロジェクトのLICENSEファイルを参照

## 貢献

1. Forkしてください
2. Feature branchを作成 (`git checkout -b feature/amazing-feature`)
3. 変更をCommit (`git commit -m 'Add amazing feature'`)
4. Branchにpush (`git push origin feature/amazing-feature`)
5. Pull Requestを作成

## サポート

- **Issues**: GitHub Issuesでバグ報告・機能要望
- **Discussions**: 質問や議論

---

作成日: 2026
バージョン: 1.0.0
