#include <Windows.h>
#include <algorithm>
#include <string>

#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#define TVTEST_PLUGIN_VERSION TVTEST_PLUGIN_VERSION_(0,0,14)
#include "TVTestPlugin.h"
#include "BonDriver_IEEE1394.h"

#define PLUGIN_NAME L"TapeControlPlugin"
#define PLUGIN_COPYRIGHT L"2026"
#define PLUGIN_DESCRIPTION L"IEEE 1394 tape transport control"

namespace {

enum {
    STATUS_ITEM_TRANSPORT_STATE = 1,
    STATUS_ITEM_TIMECODE,
    STATUS_ITEM_CONTROL_BUTTONS,
};

class CTapeControlPlugin : public TVTest::CTVTestPlugin, public TVTest::CTVTestEventHandler
{
private:
    CBonDriver_IEEE1394 *m_pBonDriver;
    UINT_PTR m_TimerID;
    std::wstring m_TransportState;
    std::wstring m_TimeCode;

    static LRESULT CALLBACK EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData)
    {
        auto *pThis = static_cast<CTapeControlPlugin *>(pClientData);
        return pThis ? pThis->HandleEvent(Event, lParam1, lParam2, pClientData) : 0;
    }

    static void CALLBACK TimerProc(HWND, UINT, UINT_PTR idEvent, DWORD)
    {
        auto *pThis = reinterpret_cast<CTapeControlPlugin *>(idEvent);
        if (pThis)
            pThis->UpdateStatus();
    }

    void NotifyStatusItemRedraw(int id)
    {
        m_pApp->StatusItemNotify(id, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
    }

    void RedrawStatusItems()
    {
        NotifyStatusItemRedraw(STATUS_ITEM_TRANSPORT_STATE);
        NotifyStatusItemRedraw(STATUS_ITEM_TIMECODE);
        NotifyStatusItemRedraw(STATUS_ITEM_CONTROL_BUTTONS);
    }

    bool LoadBonDriver()
    {
        if (m_pBonDriver)
            return true;

        m_pBonDriver = new CBonDriver_IEEE1394();
        if (!m_pBonDriver)
            return false;

        if (!m_pBonDriver->OpenTuner())
        {
            delete m_pBonDriver;
            m_pBonDriver = nullptr;
            return false;
        }

        return true;
    }

    void UnloadBonDriver()
    {
        if (!m_pBonDriver)
            return;

        m_pBonDriver->CloseTuner();
        delete m_pBonDriver;
        m_pBonDriver = nullptr;
    }

    void UpdateStatus()
    {
        if (!m_pBonDriver)
        {
            m_TransportState = L"Not connected";
            m_TimeCode = L"--:--:--:--";
            RedrawStatusItems();
            return;
        }

        m_pBonDriver->UpdateTransportState();
        m_TransportState = m_pBonDriver->GetTransportStateString();

        long hour, minute, second, frame;
        if (m_pBonDriver->GetTimeCode(&hour, &minute, &second, &frame))
        {
            wchar_t buf[64];
            swprintf_s(buf, L"%02ld:%02ld:%02ld:%02ld", hour, minute, second, frame);
            m_TimeCode = buf;
        }
        else
        {
            m_TimeCode = L"--:--:--:--";
        }

        RedrawStatusItems();
    }

    void RegisterStatusItems()
    {
        TVTest::StatusItemInfo info = {};
        info.Size = sizeof(info);
        info.Flags = 0;
        info.Style = TVTest::STATUS_ITEM_STYLE_VARIABLEWIDTH;
        info.MinHeight = 0;

        info.ID = STATUS_ITEM_TRANSPORT_STATE;
        info.pszIDText = L"TapeState";
        info.pszName = L"Tape State";
        info.MinWidth = 90;
        info.MaxWidth = 220;
        info.DefaultWidth = 140;
        m_pApp->RegisterStatusItem(&info);

        info.ID = STATUS_ITEM_TIMECODE;
        info.pszIDText = L"TimeCode";
        info.pszName = L"Time Code";
        info.MinWidth = 100;
        info.MaxWidth = 180;
        info.DefaultWidth = 120;
        m_pApp->RegisterStatusItem(&info);

        info.ID = STATUS_ITEM_CONTROL_BUTTONS;
        info.pszIDText = L"TapeControl";
        info.pszName = L"Tape Control";
        info.MinWidth = 180;
        info.MaxWidth = 320;
        info.DefaultWidth = 220;
        m_pApp->RegisterStatusItem(&info);

        TVTest::StatusItemSetInfo setInfo = {};
        setInfo.Size = sizeof(setInfo);
        setInfo.Mask = TVTest::STATUS_ITEM_SET_INFO_MASK_STATE;
        setInfo.StateMask = TVTest::STATUS_ITEM_STATE_VISIBLE;
        setInfo.State = TVTest::STATUS_ITEM_STATE_VISIBLE;

        setInfo.ID = STATUS_ITEM_TRANSPORT_STATE;
        m_pApp->SetStatusItem(&setInfo);
        setInfo.ID = STATUS_ITEM_TIMECODE;
        m_pApp->SetStatusItem(&setInfo);
        setInfo.ID = STATUS_ITEM_CONTROL_BUTTONS;
        m_pApp->SetStatusItem(&setInfo);
    }

    virtual bool OnStatusItemDraw(TVTest::StatusItemDrawInfo *pInfo) override
    {
        if (!pInfo)
            return false;

        SetBkMode(pInfo->hdc, TRANSPARENT);
        SetTextColor(pInfo->hdc, pInfo->Color);

        RECT rc = pInfo->DrawRect;
        rc.left += 4;
        rc.right -= 4;

        if (pInfo->ID == STATUS_ITEM_TRANSPORT_STATE)
        {
            DrawTextW(pInfo->hdc, m_TransportState.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            return true;
        }

        if (pInfo->ID == STATUS_ITEM_TIMECODE)
        {
            DrawTextW(pInfo->hdc, m_TimeCode.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            return true;
        }

        if (pInfo->ID == STATUS_ITEM_CONTROL_BUTTONS)
        {
            DrawTextW(pInfo->hdc, L"[STOP] [PLAY] [PAUSE] [REW] [FF]", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return true;
        }

        return false;
    }

    virtual bool OnStatusItemMouseEvent(TVTest::StatusItemMouseEventInfo *pInfo) override
    {
        if (!pInfo || pInfo->ID != STATUS_ITEM_CONTROL_BUTTONS)
            return false;

        if (pInfo->Action != TVTest::STATUS_ITEM_MOUSE_ACTION_LDOWN &&
            pInfo->Action != TVTest::STATUS_ITEM_MOUSE_ACTION_LDOUBLECLICK)
            return false;

        if (!m_pBonDriver)
            return true;

        int width = pInfo->ContentRect.right - pInfo->ContentRect.left;
        if (width <= 0)
            return true;

        int x = pInfo->CursorPos.x - pInfo->ContentRect.left;
        x = std::clamp(x, 0, width - 1);
        int index = (x * 5) / width;

        switch (index)
        {
        case 0:
            m_pBonDriver->Stop();
            break;
        case 1:
            m_pBonDriver->Play();
            break;
        case 2:
            m_pBonDriver->Pause();
            break;
        case 3:
            m_pBonDriver->Rewind();
            break;
        default:
            m_pBonDriver->FastForward();
            break;
        }

        UpdateStatus();
        return true;
    }

public:
    CTapeControlPlugin()
        : m_pBonDriver(nullptr)
        , m_TimerID(0)
        , m_TransportState(L"Initializing")
        , m_TimeCode(L"--:--:--:--")
    {
    }

    virtual ~CTapeControlPlugin()
    {
    }

    virtual bool GetPluginInfo(TVTest::PluginInfo *pInfo) override
    {
        if (!pInfo)
            return false;

        pInfo->Type = TVTest::PLUGIN_TYPE_NORMAL;
        pInfo->Flags = 0;
        pInfo->pszPluginName = PLUGIN_NAME;
        pInfo->pszCopyright = PLUGIN_COPYRIGHT;
        pInfo->pszDescription = PLUGIN_DESCRIPTION;
        return true;
    }

    virtual bool Initialize() override
    {
        if (!m_pApp->SetEventCallback(EventCallback, this))
            return false;

        RegisterStatusItems();

        if (!LoadBonDriver())
        {
            m_TransportState = L"Device open failed";
            m_TimeCode = L"--:--:--:--";
            m_pApp->AddLog(L"TapeControlPlugin: failed to open IEEE1394 device", TVTest::LOG_TYPE_WARNING);
        }
        else
        {
            m_TimerID = SetTimer(nullptr, reinterpret_cast<UINT_PTR>(this), 500, TimerProc);
        }

        UpdateStatus();
        return true;
    }

    virtual bool Finalize() override
    {
        if (m_TimerID)
        {
            KillTimer(nullptr, m_TimerID);
            m_TimerID = 0;
        }

        UnloadBonDriver();
        return true;
    }
};

} // namespace

TVTest::CTVTestPlugin *CreatePluginClass()
{
    return new CTapeControlPlugin();
}
