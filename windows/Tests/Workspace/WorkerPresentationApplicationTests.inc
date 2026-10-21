// Exercise real await/message pumping and native presentation separately from
// worker admission. No hardware or timing-dependent fast worker is required.
void applicationWorkerPresentation(){
    RestoreFixture fixture;auto &app=*fixture.app;
    app.command(mixerCommand);SetFocus(app.window);app.refreshContextPanel();
    const auto action=app.controls.at(contextActionFirst);
    restoreCheck(IsWindowEnabled(action),"Mixer context action unavailable before worker wait");
    struct Probe {
        HWND window,action;unsigned titles=0,enables=0;
        static LRESULT CALLBACK observe(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
            auto &self=*reinterpret_cast<Probe *>(data);
            if(h==self.window&&m==WM_SETTEXT)++self.titles;
            if(h==self.action&&m==WM_ENABLE)++self.enables;
            return DefSubclassProc(h,m,w,l);
        }
        Probe(HWND w,HWND a):window(w),action(a){
            restoreCheck(SetWindowSubclass(w,observe,94,reinterpret_cast<DWORD_PTR>(this))&&
                SetWindowSubclass(a,observe,94,reinterpret_cast<DWORD_PTR>(this)),"Observe worker UI transitions");
        }
        ~Probe(){RemoveWindowSubclass(window,observe,94);RemoveWindowSubclass(action,observe,94);}
    } probe(app.window,action);
    const auto title=restoreText(app.window);
    for(unsigned i=0;i<12;++i){
        std::promise<RestoreJson> gate;app.inputCompletion=&gate;
        app.postedInput=[&]{
            restoreCheck(app.busy,"Short-operation fixture did not enter the real worker gate");
            app.refreshContextPanel();app.updateTitle();
            restoreCheck(restoreText(app.window)==title&&IsWindowEnabled(action),"Short worker wait flashed native title/actions");
            restoreCheck(!app.menuCommandState(openCommand).enabled,"Deferred visual state weakened command admission");
            bool refused=false;try{app.documentOperation("mixer.get",RestoreJson::object());}
            catch(const ScreamSeq::Api::ApiError &e){refused=e.code==-32002;}
            restoreCheck(refused,"Deferred visual state allowed a reentrant document operation");
        };
        restoreCheck(PostMessageW(app.window,restoreInputMessage,0,0),"Post bounded short-operation observation");
        (void)app.await(gate.get_future());app.inputCompletion=nullptr;
    }
    restoreCheck(probe.titles==0&&probe.enables==0,"Repeated short waits churned title or enabled state");
    // A genuinely slow operation must still show busy and restore availability
    // on failure. This timer drives the real wait past its presentation delay.
    std::promise<RestoreJson> gate;app.inputCompletion=&gate;
    app.postedInput=[&]{
        restoreCheck(app.busy&&app.presentationBusy(),"Sustained wait never presented its busy state");
        restoreCheck(restoreText(app.window).find(L"Document worker busy")!=std::wstring::npos&&!IsWindowEnabled(action),
            "Sustained wait did not update native busy presentation");
        throw std::runtime_error("Owned delayed worker failure");
    };
    struct Cleanup {RestoreApplication &app;~Cleanup(){KillTimer(app.window,restoreInputDeadline);app.inputCompletion=nullptr;app.postedInput={};}} cleanup{app};
    restoreCheck(SetTimer(app.window,restoreInputDeadline,350,[](HWND h,UINT,UINT_PTR id,DWORD){
        KillTimer(h,id);auto *app=static_cast<RestoreApplication *>(reinterpret_cast<Application *>(GetWindowLongPtrW(h,GWLP_USERDATA)));
        app->deliverInput();
    }),"Bound slow worker presentation observation");
    restoreCheck(restoreRejected([&]{(void)app.await(gate.get_future());})=="Owned delayed worker failure",
        "Presentation changed the worker failure");
    restoreCheck(!app.busy&&restoreText(app.window)==title&&IsWindowEnabled(action)&&probe.titles==2&&probe.enables==2,
        "Slow failure did not restore title and native actions exactly once");
    std::cout<<"Worker presentation: 12 short waits preserve native title/actions; slow failure shows/restores busy; admission remains guarded\n";
}
