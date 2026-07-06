# BonDriver_IEEE1394

IEEE 1394（FireWire/i.LINK）デバイス用のBonDriverです。DV機器からのTSストリーム取得と、AV/C Tape Recorder/Playerの操作機能を提供します。

## 機能

- **TSストリーム受信**: IEEE 1394デバイスからMPEG-2 TSストリームを受信
- **テープ操作**: 再生、停止、早送り、巻き戻し、一時停止、録画コマンドの送信
- **状態管理**: デバイスの現在の状態（再生中、停止中など）を取得
- **BonDriver互換**: 標準的なBonDriverインターフェースに準拠

## 対応デバイス

- IEEE 1394（FireWire/i.LINK）接続のDVカメラ
- DV VTR
- DVデッキ
- その他のAV/C準拠テープレコーダー/プレイヤー

## ビルド要件

- Visual Studio 2022（または2019以降）
- Windows SDK 10.0以降
- C++17対応コンパイラ

### 必要なライブラリ

- `strmiids.lib` - DirectShow
- `quartz.lib` - DirectShow
- `ole32.lib` - COM
- `oleaut32.lib` - OLE Automation

## ビルド方法

1. Visual Studioでソリューションファイルを開く:
   ```
   BonDriver_IEEE1394.sln
   ```

2. ビルド構成を選択（Debug/Release, Win32/x64）

3. ソリューションをビルド:
   - メニュー: `ビルド` → `ソリューションのビルド`
   - または `Ctrl+Shift+B`

4. 生成されるファイル:
   - `bin\[Platform]\[Configuration]\BonDriver_IEEE1394.dll` - BonDriver DLL
   - `bin\[Platform]\[Configuration]\TestApp.exe` - テストアプリケーション

## 使用方法

### BonDriverとして使用

1. `BonDriver_IEEE1394.dll`を対応アプリケーションのBonDriverフォルダにコピー

2. アプリケーションからBonDriverを選択

3. チャンネル設定:
   - チャンネル 0: 停止
   - チャンネル 1以上: 再生開始

### テストアプリケーション

```bash
TestApp.exe
```

#### コマンド

- `P` - 再生
- `S` - 停止
- `F` - 早送り
- `R` - 巻き戻し
- `A` - 一時停止
- `T` - 状態表示
- `Q` - 終了
- `H` / `?` - ヘルプ

## API 使用例

```cpp
#include "BonDriver_IEEE1394.h"

// BonDriverの作成
IBonDriver *pDriver = CreateBonDriver();
CBonDriver_IEEE1394 *pIEEE1394Driver = static_cast<CBonDriver_IEEE1394*>(pDriver);

// デバイスを開く
if (pDriver->OpenTuner())
{
    // 再生開始
    pIEEE1394Driver->Play();

    // 状態取得
    const wchar_t* state = pIEEE1394Driver->GetTransportStateString();

    // TSストリーム受信
    BYTE *pData = nullptr;
    DWORD dwSize = 0, dwRemain = 0;
    if (pDriver->GetTsStream(&pData, &dwSize, &dwRemain))
    {
        // データ処理
    }

    // 停止
    pIEEE1394Driver->Stop();

    // クリーンアップ
    pDriver->CloseTuner();
}

pDriver->Release();
```

## 拡張メソッド

標準のBonDriverインターフェースに加えて、以下のメソッドが利用可能です:

```cpp
// テープ操作
BOOL Play(void);              // 再生
BOOL Stop(void);              // 停止
BOOL FastForward(void);       // 早送り
BOOL Rewind(void);            // 巻き戻し
BOOL Pause(void);             // 一時停止
BOOL Record(void);            // 録画

// 状態取得
TapeTransportState GetTransportState(void);        // 状態を列挙型で取得
const wchar_t* GetTransportStateString(void);      // 状態を文字列で取得
BOOL UpdateTransportState(void);                   // デバイスから状態を更新
```

## トランスポート状態

```cpp
enum class TapeTransportState
{
    Stop = 0,      // 停止
    Play,          // 再生
    FastForward,   // 早送り
    Rewind,        // 巻き戻し
    Pause,         // 一時停止
    Record,        // 録画
    Unknown        // 不明
};
```

## トラブルシューティング

### デバイスが見つからない

1. IEEE 1394デバイスが正しく接続されているか確認
2. デバイスの電源が入っているか確認
3. Windowsのデバイスマネージャーでデバイスが認識されているか確認
4. DirectShowのGraphEditでデバイスが表示されるか確認

### TSストリームが受信できない

1. テープがセットされているか確認
2. テープが巻き戻されているか確認
3. デバイスが再生状態になっているか確認
4. 信号レベルを確認（`GetSignalLevel()`）

### 操作コマンドが動作しない

1. デバイスがAV/C準拠か確認
2. `IAMExtTransport`インターフェースをサポートしているか確認
3. デバイスのファームウェアが最新か確認

## 制限事項

- IEEE 1394デバイスは1台のみサポート（複数台接続時は最初に見つかったデバイスを使用）
- デバイスによっては一部のコマンドがサポートされていない場合があります
- チャンネル切り替え機能は実装されていません（テープデバイスのため）
- 信号レベルは固定値を返します（IEEE 1394では信号レベルの概念がないため）

## ライセンス

このプロジェクトはMITライセンスの下で公開されています。

## 参考

- [TvtPlay](https://github.com/xtne6f/TvtPlay) - 参考にしたプロジェクト
- DirectShow SDK - Microsoft
- AV/C Digital Interface Command Set General Specification

## 貢献

バグレポートや機能リクエストは、GitHubのIssueまでお願いします。

## 更新履歴

### Version 1.0.0 (初回リリース)

- IEEE 1394デバイスのサポート
- 基本的なテープ操作機能
- TSストリーム受信機能
- テストアプリケーション
