#pragma once

#include "IBonDriver2.h"
#include <dshow.h>
#include <vector>
#include <queue>
#include <mutex>

#pragma comment(lib, "strmiids.lib")

// AV/C Tape Transport State
enum class TapeTransportState
{
    Stop = 0,
    Play,
    FastForward,
    Rewind,
    Pause,
    Record,
    Unknown
};

// IEEE 1394 AV/C Tape Recorder/Player BonDriver
class CBonDriver_IEEE1394 : public IBonDriver2
{
public:
    CBonDriver_IEEE1394();
    virtual ~CBonDriver_IEEE1394();

    // IBonDriver implementation
    virtual const BOOL OpenTuner(void) override;
    virtual void CloseTuner(void) override;
    virtual const BOOL SetChannel(const BYTE bCh) override;
    virtual const float GetSignalLevel(void) override;
    virtual const DWORD WaitTsStream(const DWORD dwTimeOut = 0) override;
    virtual const DWORD GetReadyCount(void) override;
    virtual const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain) override;
    virtual const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain) override;
    virtual void PurgeTsStream(void) override;
    virtual void Release(void) override;

    // IBonDriver2 implementation
    virtual LPCTSTR GetTunerName(void) override;
    virtual const BOOL IsTunerOpening(void) override;
    virtual LPCTSTR EnumTuningSpace(const DWORD dwSpace) override;
    virtual LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel) override;
    virtual const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel) override;
    virtual const DWORD GetCurSpace(void) override;
    virtual const DWORD GetCurChannel(void) override;

    STRUCT_IBONDRIVER2& GetBonStruct2() { return m_bonStruct2; }

    // AV/C Tape control methods
    BOOL SetDevicePower(long powerState);
    BOOL PowerOn(void);
    BOOL PowerOff(void);
    BOOL Standby(void);
    BOOL Play(void);
    BOOL Stop(void);
    BOOL FastForward(void);
    BOOL Rewind(void);
    BOOL Pause(void);
    BOOL Record(void);
    TapeTransportState GetTransportState(void);
    const wchar_t* GetTransportStateString(void);
    BOOL UpdateTransportState(void);
    BOOL GetTimeCode(long *pHour, long *pMinute, long *pSecond, long *pFrame);

private:
    // DirectShow graph
    IGraphBuilder *m_pGraphBuilder;
    ICaptureGraphBuilder2 *m_pCaptureGraphBuilder;
    IMediaControl *m_pMediaControl;
    IBaseFilter *m_pSourceFilter;
    IBaseFilter *m_pGrabberFilter;

    // TS Stream buffer
    std::queue<std::vector<BYTE>> m_TsQueue;
    std::vector<BYTE> m_CurrentTsBuffer;
    std::mutex m_TsQueueMutex;
    HANDLE m_hStreamEvent;
    size_t m_TsQueueBytes;
    static const DWORD TS_PACKET_SIZE = 188;
    static const size_t MAX_QUEUE_BYTES = 16 * 1024 * 1024;

    // Transport state
    TapeTransportState m_TransportState;
    DWORD m_dwCurSpace;
    DWORD m_dwCurChannel;
    STRUCT_IBONDRIVER2 m_bonStruct2;

    // Private methods
    BOOL BuildGraph(void);
    void DestroyGraph(void);
    BOOL EnumerateIEEE1394Devices(void);

    // Allow callback to access private members
    friend class CSampleGrabberCB;
};

// DLL Export
extern "C" __declspec(dllexport) IBonDriver * CreateBonDriver();
extern "C" __declspec(dllexport) const STRUCT_IBONDRIVER* CreateBonStruct();
