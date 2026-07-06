#include <Windows.h>
#include <stdio.h>
#include <conio.h>
#include "BonDriver_IEEE1394.h"

void PrintUsage()
{
    printf("\n=== IEEE 1394 BonDriver テストアプリケーション ===\n");
    printf("コマンド:\n");
    printf("  O - 電源オン\n");
    printf("  X - 電源オフ\n");
    printf("  W - スタンバイ\n");
    printf("  P - 再生\n");
    printf("  S - 停止\n");
    printf("  F - 早送り\n");
    printf("  R - 巻き戻し\n");
    printf("  A - 一時停止\n");
    printf("  T - 状態表示\n");
    printf("  Q - 終了\n");
    printf("===============================================\n\n");
}

int main()
{
    printf("IEEE 1394 BonDriver テストアプリケーション\n");
    printf("初期化中...\n");

    IBonDriver *pDriver = CreateBonDriver();
    if (!pDriver)
    {
        printf("エラー: BonDriverを作成できませんでした\n");
        return 1;
    }

    CBonDriver_IEEE1394 *pIEEE1394Driver = static_cast<CBonDriver_IEEE1394*>(pDriver);

    printf("デバイスを開いています...\n");
    if (!pDriver->OpenTuner())
    {
        printf("エラー: IEEE 1394デバイスを開けませんでした\n");
        printf("  - IEEE 1394デバイスが接続されているか確認してください\n");
        printf("  - HDV機器の電源が入っているか確認してください\n");
        pDriver->Release();
        return 1;
    }

    printf("IEEE 1394デバイスを開きました\n");
    wprintf(L"現在の状態: %ls\n", pIEEE1394Driver->GetTransportStateString());

    PrintUsage();

    BOOL bRunning = TRUE;
    DWORD dwTotalBytes = 0;
    DWORD dwPacketCount = 0;

    // TSストリーム受信スレッド（簡易版 - メインループで処理）
    while (bRunning)
    {
        // キー入力チェック
        if (_kbhit())
        {
            char ch = _getch();
            ch = toupper(ch);

            switch (ch)
            {
            case 'O':
                printf("\n電源オン...\n");
                if (pIEEE1394Driver->PowerOn())
                {
                    printf("電源をオンにしました\n");
                }
                else
                {
                    printf("電源オンに失敗しました（この機能をサポートしていないデバイスかもしれません）\n");
                }
                break;

            case 'X':
                printf("\n電源オフ...\n");
                if (pIEEE1394Driver->PowerOff())
                {
                    printf("電源をオフにしました\n");
                }
                else
                {
                    printf("電源オフに失敗しました（この機能をサポートしていないデバイスかもしれません）\n");
                }
                break;

            case 'W':
                printf("\nスタンバイ...\n");
                if (pIEEE1394Driver->Standby())
                {
                    printf("スタンバイにしました\n");
                }
                else
                {
                    printf("スタンバイに失敗しました（この機能をサポートしていないデバイスかもしれません）\n");
                }
                break;

            case 'P':
                printf("\n再生開始...\n");
                if (pIEEE1394Driver->Play())
                {
                    printf("再生を開始しました\n");
                    wprintf(L"状態: %ls\n", pIEEE1394Driver->GetTransportStateString());
                }
                else
                {
                    printf("再生開始に失敗しました\n");
                }
                break;

            case 'S':
                printf("\n停止...\n");
                if (pIEEE1394Driver->Stop())
                {
                    printf("停止しました\n");
                    wprintf(L"状態: %ls\n", pIEEE1394Driver->GetTransportStateString());
                }
                else
                {
                    printf("停止に失敗しました\n");
                }
                break;

            case 'F':
                printf("\n早送り...\n");
                if (pIEEE1394Driver->FastForward())
                {
                    printf("早送り開始しました\n");
                    wprintf(L"状態: %ls\n", pIEEE1394Driver->GetTransportStateString());
                }
                else
                {
                    printf("早送り開始に失敗しました\n");
                }
                break;

            case 'R':
                printf("\n巻き戻し...\n");
                if (pIEEE1394Driver->Rewind())
                {
                    printf("巻き戻し開始しました\n");
                    wprintf(L"状態: %ls\n", pIEEE1394Driver->GetTransportStateString());
                }
                else
                {
                    printf("巻き戻し開始に失敗しました\n");
                }
                break;

            case 'A':
                printf("\n一時停止...\n");
                if (pIEEE1394Driver->Pause())
                {
                    printf("一時停止しました\n");
                    wprintf(L"状態: %ls\n", pIEEE1394Driver->GetTransportStateString());
                }
                else
                {
                    printf("一時停止に失敗しました\n");
                }
                break;

            case 'T':
                {
                    printf("\n--- 状態情報 ---\n");
                    wprintf(L"トランスポート状態: %ls\n", pIEEE1394Driver->GetTransportStateString());
                    printf("信号レベル: %.1f\n", pDriver->GetSignalLevel());
                    printf("キュー内パケット数: %d\n", pDriver->GetReadyCount());
                    printf("受信総バイト数: %d\n", dwTotalBytes);
                    printf("受信パケット数: %d\n", dwPacketCount);
                    printf("---------------\n");
                }
                break;

            case 'Q':
                printf("\n終了します...\n");
                bRunning = FALSE;
                break;

            case 'H':
            case '?':
                PrintUsage();
                break;

            default:
                printf("\n不明なコマンド: %c\n", ch);
                printf("H または ? でヘルプを表示\n");
                break;
            }
        }

        // TSストリーム受信
        DWORD result = pDriver->WaitTsStream(100);
        if (result == WAIT_OBJECT_0)
        {
            BYTE *pData = nullptr;
            DWORD dwSize = 0, dwRemain = 0;

            while (pDriver->GetTsStream(&pData, &dwSize, &dwRemain))
            {
                if (dwSize > 0 && pData)
                {
                    dwTotalBytes += dwSize;
                    dwPacketCount++;
                }
                printf("受信中... [%d パケット, %d bytes, キュー: %d]\r", 
                    dwPacketCount, dwTotalBytes, dwRemain);
            }
        }
    }

    printf("\nクリーンアップ中...\n");
    pDriver->CloseTuner();
    pDriver->Release();

    printf("終了しました\n");
    printf("総受信バイト数: %d\n", dwTotalBytes);
    printf("総受信パケット数: %d\n", dwPacketCount);

    return 0;
}
