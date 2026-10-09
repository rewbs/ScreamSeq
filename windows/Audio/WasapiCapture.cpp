#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "WasapiCapture.hpp"
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <avrt.h>
#include <wrl/client.h>
#include <thread>

namespace ScreamSeq { namespace {
using Microsoft::WRL::ComPtr;
static_assert(std::atomic<uint64_t>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free && std::atomic<float>::is_always_lock_free,"Capture status must not lock");
struct Apartment {HRESULT value=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~Apartment(){if(SUCCEEDED(value))CoUninitialize();}};
struct FormatMemory {WAVEFORMATEX *value=nullptr;~FormatMemory(){CoTaskMemFree(value);}};
struct TextMemory {LPWSTR value=nullptr;~TextMemory(){CoTaskMemFree(value);}};
struct Event {HANDLE value=CreateEventW(nullptr,FALSE,FALSE,nullptr);~Event(){if(value)CloseHandle(value);}};
std::string utf8(const wchar_t *value) {if(!value)return {};const int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);if(n<=1)return {};std::string s(size_t(n),0);WideCharToMultiByte(CP_UTF8,0,value,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;}
std::wstring wide(const std::string &s){const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);if(!s.empty()&&!n)throw std::invalid_argument("Invalid input device identity");std::wstring value(size_t(n),0);if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),value.data(),n);return value;}
std::string explain(HRESULT hr) {
  if(hr==E_ACCESSDENIED)return "Microphone access denied. Enable Windows microphone access and Allow desktop apps in Privacy & security → Microphone.";
  if(hr==AUDCLNT_E_DEVICE_INVALIDATED)return "The selected input device was disconnected or became unavailable. The captured take is retained.";
  if(hr==AUDCLNT_E_SERVICE_NOT_RUNNING)return "Windows Audio service is not running.";
  if(hr==AUDCLNT_E_UNSUPPORTED_FORMAT)return "The selected input's native audio format is unsupported.";
  return "Microphone capture failed / HRESULT "+std::to_string(int32_t(hr));
}
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error(explain(hr));}
CaptureBuffer::Format format(const WAVEFORMATEX &w) {
  CaptureBuffer::Format f;f.channels=w.nChannels;f.bits=w.wBitsPerSample;f.validBits=w.wBitsPerSample;f.blockAlign=w.nBlockAlign;
  if(w.wFormatTag==WAVE_FORMAT_IEEE_FLOAT)f.floating=true;
  else if(w.wFormatTag==WAVE_FORMAT_EXTENSIBLE&&w.cbSize>=sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX)) {
    const auto &e=reinterpret_cast<const WAVEFORMATEXTENSIBLE &>(w);f.validBits=e.Samples.wValidBitsPerSample?e.Samples.wValidBitsPerSample:w.wBitsPerSample;
    if(IsEqualGUID(e.SubFormat,KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))f.floating=true;
    else if(!IsEqualGUID(e.SubFormat,KSDATAFORMAT_SUBTYPE_PCM))throw std::invalid_argument("Unsupported native input sample encoding");
  } else if(w.wFormatTag!=WAVE_FORMAT_PCM)throw std::invalid_argument("Unsupported native input sample encoding");
  return f;
}
class WasapiCapture final:public SampleCapture {
  CaptureBuffer buffer_;
  std::thread worker_;
  Event stop_,ready_;
  CaptureOptions options_;
  CaptureDevice device_; // Fully initialized before the ready event, then immutable.
  std::atomic<bool> capturing_{false};
  std::atomic<int32_t> error_{0};
  std::atomic<uint64_t> discontinuities_{0};
  bool initializationInvalid_=false;
  std::string initializationError_; // Published by ready event; never changed after start returns.
  void run() noexcept {
    Apartment apartment;bool signalled=false;HANDLE mmcss=nullptr;
    try {
      check(apartment.value);
      ComPtr<IMMDeviceEnumerator> enumerator;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)));
      ComPtr<IMMDevice> device;
      if(options_.device.empty())check(enumerator->GetDefaultAudioEndpoint(eCapture,eMultimedia,&device));
      else check(enumerator->GetDevice(wide(options_.device).c_str(),&device));
      ComPtr<IMMEndpoint> endpoint;check(device.As(&endpoint));EDataFlow flow;check(endpoint->GetDataFlow(&flow));
      if(flow!=eCapture)throw std::invalid_argument("Select a microphone/input endpoint, not an output or loopback device");
      TextMemory actualID;check(device->GetId(&actualID.value));device_.id=utf8(actualID.value);
      ComPtr<IPropertyStore> properties;
      if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&properties))){PROPVARIANT name;PropVariantInit(&name);if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&name))&&name.vt==VT_LPWSTR)device_.name=utf8(name.pwszVal);PropVariantClear(&name);}
      if(device_.name.empty())device_.name=device_.id;
      ComPtr<IAudioClient> client;check(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void **>(client.GetAddressOf())));
      FormatMemory mix;check(client->GetMixFormat(&mix.value));const auto f=format(*mix.value);
      CaptureBuffer::validate(f,options_.firstChannel,options_.channels);device_.channels=f.channels;
      buffer_.prepare(mix.value->nSamplesPerSec,options_.channels,options_.maxSeconds);
      Event packet; if(!packet.value)throw std::runtime_error("Cannot allocate capture event");
      check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_NOPERSIST,0,0,mix.value,nullptr));
      check(client->SetEventHandle(packet.value));ComPtr<IAudioCaptureClient> capture;check(client->GetService(IID_PPV_ARGS(&capture)));
      DWORD task=0;mmcss=AvSetMmThreadCharacteristicsW(L"Audio",&task);
      check(client->Start());capturing_=true;SetEvent(ready_.value);signalled=true;
      HANDLE waits[]{stop_.value,packet.value};bool done=false;
      while(!done) {
        const auto wait=WaitForMultipleObjects(2,waits,FALSE,2000);
        if(wait==WAIT_OBJECT_0)break;
        if(wait!=WAIT_OBJECT_0+1){error_=wait==WAIT_TIMEOUT?HRESULT_FROM_WIN32(ERROR_TIMEOUT):HRESULT_FROM_WIN32(GetLastError());break;}
        UINT32 size=0;HRESULT hr=capture->GetNextPacketSize(&size);
        while(SUCCEEDED(hr)&&size&&!done) {
          BYTE *pcm=nullptr;UINT32 frames=0;DWORD flags=0;hr=capture->GetBuffer(&pcm,&frames,&flags,nullptr,nullptr);if(FAILED(hr))break;
          if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)discontinuities_.fetch_add(1);
          buffer_.append(pcm,frames,f,options_.firstChannel,(flags&AUDCLNT_BUFFERFLAGS_SILENT)!=0);
          const auto released=capture->ReleaseBuffer(frames);if(FAILED(released)){hr=released;break;}
          done=buffer_.full()||buffer_.invalid();
          if(!done)hr=capture->GetNextPacketSize(&size);
        }
        if(FAILED(hr)){error_=int32_t(hr);break;}
      }
      client->Stop();
    } catch(const std::invalid_argument &e) {if(!signalled){initializationInvalid_=true;initializationError_=e.what();}else error_=E_FAIL;}
      catch(const std::exception &e) {if(!signalled)initializationError_=e.what();else error_=E_FAIL;}
    capturing_=false;if(mmcss)AvRevertMmThreadCharacteristics(mmcss);if(!signalled)SetEvent(ready_.value);
  }
public:
  ~WasapiCapture() override {stop();}
  void start(const CaptureOptions &o) override {
    if(worker_.joinable())throw std::logic_error("Capture already started");
    if(!stop_.value||!ready_.value)throw std::runtime_error("Cannot allocate microphone lifecycle events");
    options_=o;worker_=std::thread([this]{run();});
    if(WaitForSingleObject(ready_.value,INFINITE)!=WAIT_OBJECT_0){stop();throw std::runtime_error("Cannot wait for microphone initialization");}
    if(!initializationError_.empty()){stop();if(initializationInvalid_)throw std::invalid_argument(initializationError_);throw std::runtime_error(initializationError_);}
  }
  void stop() noexcept override {SetEvent(stop_.value);if(worker_.joinable())worker_.join();capturing_=false;}
  CaptureDevice device() const override {return device_;}
  CaptureStatus status() const override {auto s=buffer_.status();s.capturing=capturing_.load();s.discontinuities=discontinuities_.load();if(buffer_.invalid())s.error="The input device supplied invalid audio. Valid earlier frames were retained; record a new take after checking the device.";else if(auto hr=error_.load())s.error=explain(HRESULT(hr));return s;}
  std::span<const float> pcm() const override {if(capturing_.load())throw std::logic_error("Stop recording before reading PCM");return buffer_.pcm();}
};
} // namespace
std::vector<CaptureDevice> captureDevices() {
  Apartment apartment;if(FAILED(apartment.value)&&apartment.value!=RPC_E_CHANGED_MODE)check(apartment.value);
  ComPtr<IMMDeviceEnumerator> enumerator;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)));
  std::string defaultID;ComPtr<IMMDevice> defaultDevice;if(SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture,eMultimedia,&defaultDevice))){TextMemory id;if(SUCCEEDED(defaultDevice->GetId(&id.value)))defaultID=utf8(id.value);}
  ComPtr<IMMDeviceCollection> devices;check(enumerator->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&devices));UINT count=0;check(devices->GetCount(&count));std::vector<CaptureDevice> result;
  for(UINT i=0;i<count;++i){ComPtr<IMMDevice> device;check(devices->Item(i,&device));TextMemory id;check(device->GetId(&id.value));CaptureDevice info;info.id=utf8(id.value);info.isDefault=info.id==defaultID;
    ComPtr<IPropertyStore> properties;if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&properties))){PROPVARIANT v;PropVariantInit(&v);if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&v))&&v.vt==VT_LPWSTR)info.name=utf8(v.pwszVal);PropVariantClear(&v);}
    ComPtr<IAudioClient> client;if(SUCCEEDED(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void **>(client.GetAddressOf())))){FormatMemory mix;if(SUCCEEDED(client->GetMixFormat(&mix.value)))info.channels=mix.value->nChannels;}
    if(info.name.empty())info.name=info.id;result.push_back(std::move(info));
  }return result;
}
std::unique_ptr<SampleCapture> makeWasapiCapture(){return std::make_unique<WasapiCapture>();}
} // namespace ScreamSeq
