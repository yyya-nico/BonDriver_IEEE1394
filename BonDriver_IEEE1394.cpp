#define BONSDK_IMPLEMENT
#include "BonDriver_IEEE1394.h"

static IBonDriver* g_pBonThis = nullptr;

// Sample Grabber CLSID and IID
DEFINE_GUID(CLSID_SampleGrabber, 0xC1F400A0, 0x3F08, 0x11d3, 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37);
DEFINE_GUID(IID_ISampleGrabber, 0x6B652FFF, 0x11FE, 0x4fce, 0x92, 0xAD, 0x02, 0x66, 0xB5, 0xD7, 0xC7, 0x8F);
DEFINE_GUID(IID_ISampleGrabberCB, 0x0579154A, 0x2B53, 0x4994, 0xB0, 0xD0, 0xE7, 0x73, 0x14, 0x8E, 0xFF, 0x85);

// ISampleGrabberCB interface
interface __declspec(uuid("0579154A-2B53-4994-B0D0-E773148EFF85"))
ISampleGrabberCB : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double SampleTime, IMediaSample *pSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double SampleTime, BYTE *pBuffer, long BufferLen) = 0;
};

// ISampleGrabber interface
interface ISampleGrabber : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL OneShot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE* pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE* pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL BufferThem) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long* pBufferSize, long* pBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample** ppSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB* pCallback, long WhichMethodToCallback) = 0;
};

// Sample Grabber Callback
class CSampleGrabberCB : public ISampleGrabberCB
{
public:
    CSampleGrabberCB(CBonDriver_IEEE1394* pDriver) : m_pDriver(pDriver), m_RefCount(1) {}

    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_RefCount); }
    STDMETHODIMP_(ULONG) Release()
    {
        ULONG count = InterlockedDecrement(&m_RefCount);
        if (count == 0) delete this;
        return count;
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (riid == IID_ISampleGrabberCB || riid == IID_IUnknown)
        {
            *ppv = static_cast<ISampleGrabberCB*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP SampleCB(double Time, IMediaSample* pSample) { return E_NOTIMPL; }

    STDMETHODIMP BufferCB(double Time, BYTE* pBuffer, long BufferLen)
    {
        if (!m_pDriver || !pBuffer || BufferLen <= 0)
            return S_OK;

        std::lock_guard<std::mutex> lock(m_pDriver->m_TsQueueMutex);

        const size_t incomingSize = static_cast<size_t>(BufferLen);

        // 入力1塊が上限を超える場合は末尾側のみ保持
        if (incomingSize > CBonDriver_IEEE1394::MAX_QUEUE_BYTES)
        {
            pBuffer += incomingSize - CBonDriver_IEEE1394::MAX_QUEUE_BYTES;
            BufferLen = static_cast<long>(CBonDriver_IEEE1394::MAX_QUEUE_BYTES);
        }

        // キューサイズ制限(バイト単位)
        while (!m_pDriver->m_TsQueue.empty() &&
            m_pDriver->m_TsQueueBytes + static_cast<size_t>(BufferLen) > CBonDriver_IEEE1394::MAX_QUEUE_BYTES)
        {
            m_pDriver->m_TsQueueBytes -= m_pDriver->m_TsQueue.front().size();
            m_pDriver->m_TsQueue.pop();
        }

        std::vector<BYTE> data(pBuffer, pBuffer + BufferLen);
        m_pDriver->m_TsQueueBytes += data.size();
        m_pDriver->m_TsQueue.push(std::move(data));
        SetEvent(m_pDriver->m_hStreamEvent);

        return S_OK;
    }

private:
    CBonDriver_IEEE1394* m_pDriver;
    LONG m_RefCount;
    friend class CBonDriver_IEEE1394;
};

CBonDriver_IEEE1394::CBonDriver_IEEE1394()
    : m_pGraphBuilder(nullptr)
    , m_pCaptureGraphBuilder(nullptr)
    , m_pMediaControl(nullptr)
    , m_pSourceFilter(nullptr)
    , m_pGrabberFilter(nullptr)
    , m_hStreamEvent(nullptr)
    , m_TsQueueBytes(0)
    , m_TransportState(TapeTransportState::Unknown)
    , m_dwCurSpace(0xFFFFFFFFUL)
    , m_dwCurChannel(0xFFFFFFFFUL)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    m_hStreamEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

CBonDriver_IEEE1394::~CBonDriver_IEEE1394()
{
    CloseTuner();

    if (g_pBonThis == this)
        g_pBonThis = nullptr;

    if (m_hStreamEvent)
    {
        CloseHandle(m_hStreamEvent);
        m_hStreamEvent = nullptr;
    }

    CoUninitialize();
}

const BOOL CBonDriver_IEEE1394::OpenTuner(void)
{
    if (m_pGraphBuilder)
        return TRUE; // Already opened

    if (!BuildGraph())
    {
        OutputDebugStringW(L"BonDriver_IEEE1394: BuildGraph failed in OpenTuner().\n");
        return FALSE;
    }
    return TRUE;
}

void CBonDriver_IEEE1394::CloseTuner(void)
{
    Stop();
    DestroyGraph();
    PurgeTsStream();
    m_TransportState = TapeTransportState::Unknown;
    m_dwCurSpace = 0xFFFFFFFFUL;
    m_dwCurChannel = 0xFFFFFFFFUL;
}

const BOOL CBonDriver_IEEE1394::SetChannel(const BYTE bCh)
{
    // IEEE 1394デバイスはチャンネル設定の概念がないため、
    // ここでは単純にPlayを開始する
    if (bCh == 0)
        return Stop();
    else
        return Play();
}

LPCTSTR CBonDriver_IEEE1394::GetTunerName(void)
{
    return TEXT("IEEE1394 AV/C Tape");
}

const BOOL CBonDriver_IEEE1394::IsTunerOpening(void)
{
    return m_pGraphBuilder ? TRUE : FALSE;
}

LPCTSTR CBonDriver_IEEE1394::EnumTuningSpace(const DWORD dwSpace)
{
    return dwSpace == 0 ? TEXT("IEEE1394") : nullptr;
}

LPCTSTR CBonDriver_IEEE1394::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
    if (dwSpace != 0)
        return nullptr;

    switch (dwChannel)
    {
    case 0:
        return TEXT("Stop");
    case 1:
        return TEXT("Play");
    default:
        return nullptr;
    }
}

const BOOL CBonDriver_IEEE1394::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
    if (!IsTunerOpening() || dwSpace != 0 || dwChannel > 1)
        return FALSE;

    if (!SetChannel(static_cast<BYTE>(dwChannel)))
        return FALSE;

    m_dwCurSpace = dwSpace;
    m_dwCurChannel = dwChannel;
    return TRUE;
}

const DWORD CBonDriver_IEEE1394::GetCurSpace(void)
{
    return m_dwCurSpace;
}

const DWORD CBonDriver_IEEE1394::GetCurChannel(void)
{
    return m_dwCurChannel;
}

const float CBonDriver_IEEE1394::GetSignalLevel(void)
{
    // IEEE 1394では信号レベルの概念がないため、再生中かどうかで判定
    return (m_TransportState == TapeTransportState::Play) ? 100.0f : 0.0f;
}

const DWORD CBonDriver_IEEE1394::WaitTsStream(const DWORD dwTimeOut)
{
    if (!m_hStreamEvent)
        return WAIT_ABANDONED;

    return WaitForSingleObject(m_hStreamEvent, dwTimeOut ? dwTimeOut : INFINITE);
}

const DWORD CBonDriver_IEEE1394::GetReadyCount(void)
{
    std::lock_guard<std::mutex> lock(m_TsQueueMutex);
    return static_cast<DWORD>(m_TsQueue.size());
}

const BOOL CBonDriver_IEEE1394::GetTsStream(BYTE* pDst, DWORD* pdwSize, DWORD* pdwRemain)
{
    if (!pDst || !pdwSize || !pdwRemain)
        return FALSE;

    std::lock_guard<std::mutex> lock(m_TsQueueMutex);

    if (m_TsQueue.empty())
    {
        *pdwSize = 0;
        *pdwRemain = 0;
        return TRUE;
    }

    auto& front = m_TsQueue.front();
    DWORD copySize = min(*pdwSize, static_cast<DWORD>(front.size()));
    memcpy(pDst, front.data(), copySize);
    *pdwSize = copySize;
    m_TsQueueBytes -= front.size();
    m_TsQueue.pop();
    *pdwRemain = static_cast<DWORD>(m_TsQueue.size());
    if (!m_TsQueue.empty())
        SetEvent(m_hStreamEvent);

    return TRUE;
}

const BOOL CBonDriver_IEEE1394::GetTsStream(BYTE** ppDst, DWORD* pdwSize, DWORD* pdwRemain)
{
    if (!ppDst || !pdwSize || !pdwRemain)
        return FALSE;

    std::lock_guard<std::mutex> lock(m_TsQueueMutex);

    if (m_TsQueue.empty())
    {
        *ppDst = nullptr;
        *pdwSize = 0;
        *pdwRemain = 0;
        return TRUE;
    }

    auto& front = m_TsQueue.front();
    m_CurrentTsBuffer = std::move(front);
    m_TsQueueBytes -= m_CurrentTsBuffer.size();
    m_TsQueue.pop();

    *ppDst = m_CurrentTsBuffer.empty() ? nullptr : m_CurrentTsBuffer.data();
    *pdwSize = static_cast<DWORD>(m_CurrentTsBuffer.size());
    *pdwRemain = static_cast<DWORD>(m_TsQueue.size());
    if (!m_TsQueue.empty())
        SetEvent(m_hStreamEvent);

    return TRUE;
}

void CBonDriver_IEEE1394::PurgeTsStream(void)
{
    std::lock_guard<std::mutex> lock(m_TsQueueMutex);
    while (!m_TsQueue.empty())
        m_TsQueue.pop();
    m_CurrentTsBuffer.clear();
    m_TsQueueBytes = 0;
}

void CBonDriver_IEEE1394::Release(void)
{
    delete this;
}

BOOL CBonDriver_IEEE1394::BuildGraph(void)
{
    HRESULT hr;

    // Create Filter Graph
    hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
        IID_IGraphBuilder, (void**)&m_pGraphBuilder);
    if (FAILED(hr))
    {
        OutputDebugStringW(L"BonDriver_IEEE1394: CoCreateInstance(CLSID_FilterGraph) failed.\n");
        return FALSE;
    }

    // Create Capture Graph Builder
    hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER,
        IID_ICaptureGraphBuilder2, (void**)&m_pCaptureGraphBuilder);
    if (FAILED(hr))
    {
        OutputDebugStringW(L"BonDriver_IEEE1394: CoCreateInstance(CLSID_CaptureGraphBuilder2) failed.\n");
        DestroyGraph();
        return FALSE;
    }

    m_pCaptureGraphBuilder->SetFiltergraph(m_pGraphBuilder);

    // Enumerate IEEE 1394 devices
    if (!EnumerateIEEE1394Devices())
    {
        OutputDebugStringW(L"BonDriver_IEEE1394: EnumerateIEEE1394Devices() failed.\n");
        DestroyGraph();
        return FALSE;
    }

    // Create Sample Grabber
    hr = CoCreateInstance(CLSID_SampleGrabber, nullptr, CLSCTX_INPROC_SERVER,
        IID_IBaseFilter, (void**)&m_pGrabberFilter);
    if (FAILED(hr))
    {
        OutputDebugStringW(L"BonDriver_IEEE1394: CoCreateInstance(CLSID_SampleGrabber) failed.\n");
        DestroyGraph();
        return FALSE;
    }

    // Configure Sample Grabber
    ISampleGrabber* pGrabber = nullptr;
    hr = m_pGrabberFilter->QueryInterface(IID_ISampleGrabber, (void**)&pGrabber);
    if (SUCCEEDED(hr))
    {
        AM_MEDIA_TYPE mt;
        ZeroMemory(&mt, sizeof(AM_MEDIA_TYPE));
        mt.majortype = MEDIATYPE_Stream;
        mt.subtype = MEDIASUBTYPE_MPEG2_TRANSPORT;

        pGrabber->SetMediaType(&mt);
        pGrabber->SetBufferSamples(FALSE);
        pGrabber->SetOneShot(FALSE);

        CSampleGrabberCB* pCallback = new CSampleGrabberCB(this);
        pGrabber->SetCallback(pCallback, 1); // BufferCB mode
        pCallback->Release();

        pGrabber->Release();
    }

    // Add filters to graph
    m_pGraphBuilder->AddFilter(m_pSourceFilter, L"IEEE1394 Source");
    m_pGraphBuilder->AddFilter(m_pGrabberFilter, L"Sample Grabber");

    // Connect filters
    hr = m_pCaptureGraphBuilder->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Stream,
        m_pSourceFilter, nullptr, m_pGrabberFilter);

    if (FAILED(hr))
    {
        // Try without category
        hr = m_pCaptureGraphBuilder->RenderStream(nullptr, &MEDIATYPE_Stream,
            m_pSourceFilter, nullptr, m_pGrabberFilter);
    }

    if (FAILED(hr))
    {
        DestroyGraph();
        OutputDebugStringW(L"BonDriver_IEEE1394: RenderStream failed.\n");
        return FALSE;
    }

    // Get Media Control interface
    hr = m_pGraphBuilder->QueryInterface(IID_IMediaControl, (void**)&m_pMediaControl);
    if (FAILED(hr))
    {
        OutputDebugStringW(L"BonDriver_IEEE1394: QueryInterface(IID_IMediaControl) failed.\n");
        DestroyGraph();
        return FALSE;
    }

    if (m_pMediaControl) {
        m_pMediaControl->Run(); // Start the graph to begin receiving data
    }

    // Try to power on the device
    PowerOn();

    m_TransportState = TapeTransportState::Stop;
    return TRUE;
}

void CBonDriver_IEEE1394::DestroyGraph(void)
{
    if (m_pMediaControl)
    {
        m_pMediaControl->Stop();
        m_pMediaControl->Release();
        m_pMediaControl = nullptr;
    }

    if (m_pGrabberFilter)
    {
        if (m_pGraphBuilder)
            m_pGraphBuilder->RemoveFilter(m_pGrabberFilter);
        m_pGrabberFilter->Release();
        m_pGrabberFilter = nullptr;
    }

    if (m_pSourceFilter)
    {
        if (m_pGraphBuilder)
            m_pGraphBuilder->RemoveFilter(m_pSourceFilter);
        m_pSourceFilter->Release();
        m_pSourceFilter = nullptr;
    }

    if (m_pCaptureGraphBuilder)
    {
        m_pCaptureGraphBuilder->Release();
        m_pCaptureGraphBuilder = nullptr;
    }

    if (m_pGraphBuilder)
    {
        m_pGraphBuilder->Release();
        m_pGraphBuilder = nullptr;
    }
}

BOOL CBonDriver_IEEE1394::EnumerateIEEE1394Devices(void)
{
    ICreateDevEnum* pDevEnum = nullptr;
    IEnumMoniker* pEnum = nullptr;
    IMoniker* pMoniker = nullptr;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
        IID_ICreateDevEnum, (void**)&pDevEnum);
    if (FAILED(hr))
        return FALSE;

    const CLSID* categories[] = {
        &CLSID_VideoInputDeviceCategory,
        &CLSID_LegacyAmFilterCategory,
    };

    for (const CLSID* pCategory : categories)
    {
        pEnum = nullptr;
        hr = pDevEnum->CreateClassEnumerator(*pCategory, &pEnum, 0);
        if (hr != S_OK)
            continue;

        while (pEnum->Next(1, &pMoniker, nullptr) == S_OK)
        {
            IBaseFilter* pFilter = nullptr;
            hr = pMoniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, (void**)&pFilter);
            if (SUCCEEDED(hr))
            {
                // Check if this device supports IAMExtTransport (digital VCR control)
                IAMExtTransport* pTransport = nullptr;
                hr = pFilter->QueryInterface(IID_IAMExtTransport, (void**)&pTransport);
                if (SUCCEEDED(hr))
                {
                    // This device supports VCR control - use it
                    pTransport->Release();
                    m_pSourceFilter = pFilter;
                    pMoniker->Release();
                    pEnum->Release();
                    pDevEnum->Release();
                    return TRUE;
                }
                pFilter->Release();
            }
            pMoniker->Release();
        }
        pEnum->Release();
    }

    pDevEnum->Release();
    return FALSE;
}

BOOL CBonDriver_IEEE1394::SetDevicePower(long powerState)
{
    if (!m_pSourceFilter)
        return FALSE;

    IAMExtDevice* pExtDevice = nullptr;
    HRESULT hr = m_pSourceFilter->QueryInterface(IID_IAMExtDevice, (void**)&pExtDevice);
    if (FAILED(hr))
        return FALSE;

    hr = pExtDevice->put_DevicePower(powerState);
    pExtDevice->Release();

    return SUCCEEDED(hr) ? TRUE : FALSE;
}

BOOL CBonDriver_IEEE1394::PowerOn(void)
{
    return SetDevicePower(ED_POWER_ON);
}

BOOL CBonDriver_IEEE1394::PowerOff(void)
{
    return SetDevicePower(ED_POWER_OFF);
}

BOOL CBonDriver_IEEE1394::Standby(void)
{
    return SetDevicePower(ED_POWER_STANDBY);
}

BOOL CBonDriver_IEEE1394::Play(void)
{
    if (!m_pMediaControl)
        return FALSE;

    // Try IAMExtTransport interface
    IAMExtTransport* pTransport = nullptr;
    HRESULT hr = m_pSourceFilter->QueryInterface(IID_IAMExtTransport, (void**)&pTransport);
    if (SUCCEEDED(hr))
    {
        hr = pTransport->put_Mode(ED_MODE_PLAY); // ED_MODE_PLAY = Play
        pTransport->Release();
        if (SUCCEEDED(hr))
        {
            m_TransportState = TapeTransportState::Play;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CBonDriver_IEEE1394::Stop(void)
{
    if (!m_pMediaControl)
        return FALSE;
        
    // Try IAMExtTransport interface
    IAMExtTransport* pTransport = nullptr;
    HRESULT hr = m_pSourceFilter->QueryInterface(IID_IAMExtTransport, (void**)&pTransport);
    if (SUCCEEDED(hr))
    {
        hr = pTransport->put_Mode(ED_MODE_STOP); // ED_MODE_STOP = Stop
        pTransport->Release();
        if (SUCCEEDED(hr))
        {
            m_TransportState = TapeTransportState::Stop;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CBonDriver_IEEE1394::Pause(void)
{
    if (!m_pMediaControl)
        return FALSE;
        
    // Try IAMExtTransport interface
    IAMExtTransport* pTransport = nullptr;
    HRESULT hr = m_pSourceFilter->QueryInterface(IID_IAMExtTransport, (void**)&pTransport);
    if (SUCCEEDED(hr))
    {
        hr = pTransport->put_Mode(ED_MODE_FREEZE); // ED_MODE_FREEZE = Pause
        pTransport->Release();
        if (SUCCEEDED(hr))
        {
            m_TransportState = TapeTransportState::Pause;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CBonDriver_IEEE1394::FastForward(void)
{
    if (!m_pSourceFilter)
        return FALSE;

    // Try IAMExtTransport interface
    IAMExtTransport* pTransport = nullptr;
    HRESULT hr = m_pSourceFilter->QueryInterface(IID_IAMExtTransport, (void**)&pTransport);
    if (SUCCEEDED(hr))
    {
        hr = pTransport->put_Mode(ED_MODE_FF); // ED_MODE_FF = Fast Forward
        pTransport->Release();
        if (SUCCEEDED(hr))
        {
            m_TransportState = TapeTransportState::FastForward;
            return TRUE;
        }
    }

    // Fallback: just continue playing
    return Play();
}

BOOL CBonDriver_IEEE1394::Rewind(void)
{
    if (!m_pSourceFilter)
        return FALSE;

    // Try IAMExtTransport interface
    IAMExtTransport* pTransport = nullptr;
    HRESULT hr = m_pSourceFilter->QueryInterface(IID_IAMExtTransport, (void**)&pTransport);
    if (SUCCEEDED(hr))
    {
        hr = pTransport->put_Mode(ED_MODE_REW); // ED_MODE_REW = Rewind
        pTransport->Release();
        if (SUCCEEDED(hr))
        {
            m_TransportState = TapeTransportState::Rewind;
            return TRUE;
        }
    }

    return FALSE;
}

BOOL CBonDriver_IEEE1394::Record(void)
{
    if (!m_pSourceFilter)
        return FALSE;

    // Try IAMExtTransport interface
    IAMExtTransport* pTransport = nullptr;
    HRESULT hr = m_pSourceFilter->QueryInterface(IID_IAMExtTransport, (void**)&pTransport);
    if (SUCCEEDED(hr))
    {
        hr = pTransport->put_Mode(ED_MODE_RECORD); // ED_MODE_RECORD = Record
        pTransport->Release();
        if (SUCCEEDED(hr))
        {
            m_TransportState = TapeTransportState::Record;
            return TRUE;
        }
    }

    return FALSE;
}

TapeTransportState CBonDriver_IEEE1394::GetTransportState(void)
{
    UpdateTransportState();
    return m_TransportState;
}

const wchar_t* CBonDriver_IEEE1394::GetTransportStateString(void)
{
    UpdateTransportState();

    switch (m_TransportState)
    {
    case TapeTransportState::Stop:        return L"停止";
    case TapeTransportState::Play:        return L"再生";
    case TapeTransportState::FastForward: return L"早送り";
    case TapeTransportState::Rewind:      return L"巻き戻し";
    case TapeTransportState::Pause:       return L"一時停止";
    case TapeTransportState::Record:      return L"録画";
    case TapeTransportState::Unknown:     return L"不明";
    default:                               return L"エラー";
    }
}

// Update Transport State from device
BOOL CBonDriver_IEEE1394::UpdateTransportState(void)
{
    if (!m_pSourceFilter)
        return FALSE;

    IAMExtTransport* pTransport = nullptr;
    HRESULT hr = m_pSourceFilter->QueryInterface(IID_IAMExtTransport, (void**)&pTransport);
    if (SUCCEEDED(hr))
    {
        long mode = 0;
        hr = pTransport->get_Mode(&mode);
        pTransport->Release();

        if (SUCCEEDED(hr))
        {
            switch (mode)
            {
            case ED_MODE_STOP:
                m_TransportState = TapeTransportState::Stop;
                break;
            case ED_MODE_PLAY:
                m_TransportState = TapeTransportState::Play;
                break;
            case ED_MODE_FF:
                m_TransportState = TapeTransportState::FastForward;
                break;
            case ED_MODE_REW:
                m_TransportState = TapeTransportState::Rewind;
                break;
            case ED_MODE_RECORD:
                m_TransportState = TapeTransportState::Record;
                break;
            case ED_MODE_FREEZE:
                m_TransportState = TapeTransportState::Pause;
                break;
            default:
                m_TransportState = TapeTransportState::Unknown;
                break;
            }
            return TRUE;
        }
    }

    return FALSE;
}

BOOL CBonDriver_IEEE1394::GetTimeCode(long *pHour, long *pMinute, long *pSecond, long *pFrame)
{
    if (!m_pSourceFilter || !pHour || !pMinute || !pSecond || !pFrame)
        return FALSE;

    IAMExtTransport* pTransport = nullptr;
    HRESULT hr = m_pSourceFilter->QueryInterface(IID_IAMExtTransport, (void**)&pTransport);
    if (SUCCEEDED(hr))
    {
        // IAMTimecodeReader インターフェースを試す
        IAMTimecodeReader* pTimecodeReader = nullptr;
        hr = m_pSourceFilter->QueryInterface(IID_IAMTimecodeReader, (void**)&pTimecodeReader);
        if (SUCCEEDED(hr))
        {
            TIMECODE_SAMPLE timecode;
            hr = pTimecodeReader->GetTimecode(&timecode);
            if (SUCCEEDED(hr))
            {
                *pHour = timecode.timecode.dwFrames / (30 * 60 * 60);
                *pMinute = (timecode.timecode.dwFrames / (30 * 60)) % 60;
                *pSecond = (timecode.timecode.dwFrames / 30) % 60;
                *pFrame = timecode.timecode.dwFrames % 30;

                pTimecodeReader->Release();
                pTransport->Release();
                return TRUE;
            }
            pTimecodeReader->Release();
        }

        pTransport->Release();
    }

    // タイムコード取得失敗時はゼロを返す
    *pHour = 0;
    *pMinute = 0;
    *pSecond = 0;
    *pFrame = 0;
    return FALSE;
}

// DLL Export
extern "C" BONAPI IBonDriver* CreateBonDriver()
{
    if (!g_pBonThis)
        g_pBonThis = new CBonDriver_IEEE1394();
    return g_pBonThis;
}

extern "C" BONAPI const STRUCT_IBONDRIVER* CreateBonStruct()
{
    CBonDriver_IEEE1394* pDriver = static_cast<CBonDriver_IEEE1394*>(CreateBonDriver());
    return &pDriver->GetBonStruct2().Initialize(pDriver, nullptr);
}