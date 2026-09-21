#pragma once
#include "WindowsVST3.hpp"
namespace Tracker::WindowsVST3 {
class NativeBackend final:public PluginBackend {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 NativeBackend(const PluginState &,double,bool,const std::string &hash);
 ~NativeBackend();
 bool process(float *,uint32_t,uint64_t,const float *const *,uint32_t,const PluginTransport &) noexcept override;
 bool parameter(uint32_t,double,uint32_t) noexcept override;
 bool supportsSampleOffsetParameters()const noexcept override{return true;}
 void transport(const PluginTransport &) noexcept override;
 bool midi(uint8_t,uint8_t,uint8_t)noexcept override;
 const std::vector<PluginAudioBus>&buses()const override;
 const float *auxiliaryOutput(uint32_t)const noexcept override;
 std::vector<PluginParameter> parameters()const override;
 std::vector<PluginProgram> programs()const override;
 void loadProgram(const std::string &)override;
 PluginState state()const override;
 double latency()const override;
 double tail()const override;
 void showEditor()override;
 void closeEditor()override;
 bool editorOpen()const override;
 bool popEdit(uint32_t &,float &)noexcept override;
};
}
