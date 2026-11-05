// Smoke-only owning environment read; a read failure cannot silently enable cleanup.
std::optional<std::string> mainMenuSmokeEnvironment(const char* name) {
    char* value=nullptr;std::size_t size=0;
    if(_dupenv_s(&value,&size,name)!=0)throw std::runtime_error("main menu smoke environment read failed");
    std::unique_ptr<char,decltype(&std::free)> owned(value,&std::free);
    return value?std::optional<std::string>(value):std::nullopt;
}
// Complete old private class; injected sources remain instance-local.
#include "audio/menu/diagnostics/main_menu_music_original/MenuAudio.original.inl"
struct MainMusicClockFixtureClock final : RuntimeUiClock {
    std::uint64_t value=0;
    std::vector<std::uint64_t> sequence;
    mutable std::size_t index=0;
    mutable std::vector<std::uint64_t> reads;
    MainMusicClockFixtureClock() {reads.reserve(128);}
    std::uint64_t nowMs() const noexcept override {
        const auto result=index<sequence.size()?sequence[index++]:value;
        reads.push_back(result);return result;
    }
};
struct MainMusicClockObservation {
    std::vector<std::string> states;
    std::vector<std::uint64_t> reads;
    unsigned long long position=0;
};
struct MainMusicClockChecks {
    std::size_t checks=0,failures=0;
    void expect(bool value,const char* message) {
        ++checks;if(!value){++failures;std::cout<<"Main music clock failure: "<<message<<'\n';}
    }
};
std::wstring mainMusicClockNativeMode() {
    wchar_t text[64]{};
    return mciSendStringW(L"status concord_runtime_intro mode",text,64,nullptr)==0?std::wstring(text):L"<error>";
}
template<class Audio> MainMusicClockObservation runMainMusicClockScene(MainMusicClockChecks& check) {
    MainMusicClockFixtureClock clock;MainMusicClockObservation observation;
    {
        Audio audio;
        if constexpr(requires{audio.clock;}) audio.clock=&clock;
        else audio.fixtureClock=&clock;
        const auto observe=[&] {
            std::ostringstream text;
            const auto wide=[&](const std::wstring& value){for(wchar_t unit:value)text<<std::hex<<static_cast<unsigned int>(unit)<<',';text<<';'<<std::dec;};
            text<<audio.enabled<<' '<<audio.musicVolumePercent<<' '<<audio.musicOpen<<' '<<audio.nextMusicProbe<<' '<<clock.reads.size()<<' ';
            wide(audio.assetRoot.native());wide(audio.cacheRoot.native());wide(audio.musicPath.native());
            std::vector<std::pair<std::string,std::string>> samples(audio.samples.begin(),audio.samples.end());std::sort(samples.begin(),samples.end());
            for(const auto& [key,value]:samples)text<<std::quoted(key)<<' '<<std::quoted(value)<<' ';
            std::vector<std::string> names;for(const auto& [key,value]:audio.waves){(void)value;names.push_back(key);}std::sort(names.begin(),names.end());
            for(const auto& key:names){text<<std::quoted(key)<<' '<<audio.waves.at(key).has_value()<<' ';if(audio.waves.at(key))wide(*audio.waves.at(key));}
            observation.states.push_back(text.str());
        };
        audio.enabled=false;audio.startMusic("a.wav");audio.ensureMusicPlaying();
        check.expect(clock.reads.empty()&&!audio.musicOpen,"disabled original guards never read source");observe();
        audio.enabled=true;audio.musicVolumePercent=0;audio.startMusic("a.wav");
        check.expect(clock.reads.empty()&&!audio.musicOpen,"zero-volume start retains original early guard");observe();
        audio.musicVolumePercent=100;audio.startMusic({});audio.startMusic("missing.wav");
        check.expect(clock.reads.empty()&&audio.musicPath.empty(),"empty/missing file guard before sampling");observe();
        clock.value=100;audio.startMusic("a.wav");
        check.expect(clock.reads.size()==1&&audio.nextMusicProbe==1100&&audio.musicOpen&&audio.musicPath==fs::path("a.wav"),"original start one read plus1000ms");
        check.expect(mainMusicClockNativeMode()==L"playing","real MCI acknowledges own nonempty silent PCM");
        Sleep(60);wchar_t position[64]{};
        const auto positionError=mciSendStringW(L"status concord_runtime_intro position",position,64,nullptr);
        observation.position=std::wcstoull(position,nullptr,10);
        check.expect(positionError==0&&observation.position>0,"actual device position advances; hearing/loopback not claimed");observe();
        audio.startMusic("a.wav");check.expect(clock.reads.size()==1&&audio.nextMusicProbe==1100,"same open path does not restart or sample");observe();
        check.expect(mciSendStringW(L"stop concord_runtime_intro",nullptr,0,nullptr)==0,"own device stopped for deadline check");
        clock.value=1099;audio.ensureMusicPlaying();check.expect(clock.reads.size()==2&&audio.nextMusicProbe==1100&&mainMusicClockNativeMode()==L"stopped","deadline-minus1 only samples");observe();
        clock.value=1100;audio.ensureMusicPlaying();check.expect(clock.reads.size()==3&&audio.nextMusicProbe==2100&&mainMusicClockNativeMode()==L"playing","exact deadline restarts without nested source read");observe();
        clock.value=50;audio.ensureMusicPlaying();check.expect(clock.reads.size()==4&&audio.nextMusicProbe==2100,"backwards input keeps unsigned original comparison");observe();
        audio.enabled=false;audio.ensureMusicPlaying();audio.startMusic("b.wav");check.expect(clock.reads.size()==4&&audio.musicPath==fs::path("a.wav"),"disabled retains playing file");observe();audio.enabled=true;
        check.expect(mciSendStringW(L"close concord_runtime_intro",nullptr,0,nullptr)==0,"actual own alias lost for recovery");
        clock.sequence={2100,2500};clock.index=0;audio.ensureMusicPlaying();
        check.expect(clock.reads.size()==6&&clock.index==2&&audio.nextMusicProbe==3500&&audio.musicOpen&&mainMusicClockNativeMode()==L"playing","poll plus nested reopen sample independently after real device loss");observe();
        clock.sequence.clear();clock.index=0;clock.value=5000;audio.startMusic("b.wav");
        check.expect(clock.reads.size()==7&&audio.musicPath==fs::path("b.wav")&&audio.nextMusicProbe==6000&&mainMusicClockNativeMode()==L"playing","different valid file stops old and samples new start");observe();
        audio.musicVolumePercent=0;clock.value=6000;audio.ensureMusicPlaying();
        check.expect(clock.reads.size()==8&&audio.nextMusicProbe==7000&&audio.musicOpen,"original poll intentionally has no zero-volume guard");observe();audio.musicVolumePercent=100;
        audio.stopMusic();audio.ensureMusicPlaying();check.expect(clock.reads.size()==8&&!audio.musicOpen&&audio.musicPath.empty()&&audio.nextMusicProbe==0&&mainMusicClockNativeMode()==L"<error>","stop and empty poll leave source alone and close actual alias");observe();
        clock.value=7000;audio.startMusic("bad.wav");check.expect(clock.reads.size()==9&&!audio.musicOpen&&audio.musicPath==fs::path("bad.wav")&&audio.nextMusicProbe==8000,"failed open retains requested path and exact next probe");observe();
        clock.value=8000;audio.ensureMusicPlaying();check.expect(clock.reads.size()==11&&!audio.musicOpen&&audio.nextMusicProbe==9000,"failed-open retry retains two separate source reads");observe();
        clock.value=std::numeric_limits<std::uint64_t>::max()-500;audio.startMusic("a.wav");check.expect(clock.reads.size()==12&&audio.musicOpen&&audio.nextMusicProbe==499,"original+1000 uint64 modulo without saturation");observe();
        check.expect(mciSendStringW(L"stop concord_runtime_intro",nullptr,0,nullptr)==0,"own alias stopped around wrap");
        clock.value=498;audio.ensureMusicPlaying();check.expect(clock.reads.size()==13&&audio.nextMusicProbe==499&&mainMusicClockNativeMode()==L"stopped","wrapped deadline-minus1 guard");observe();
        clock.value=499;audio.ensureMusicPlaying();check.expect(clock.reads.size()==14&&audio.nextMusicProbe==1499&&mainMusicClockNativeMode()==L"playing","wrapped exact deadline restarts actual device");observe();
        audio.stopMusic();check.expect(clock.reads.size()==14&&mainMusicClockNativeMode()==L"<error>"&&!audio.musicOpen&&audio.musicPath.empty()&&audio.nextMusicProbe==0,"final actual alias closed with no extra sampling");observe();
    }
    check.expect(clock.reads.size()==14,"destruction never consults borrowed source");observation.reads=clock.reads;return observation;
}
int runMainMenuMusicClockSmoke() {
    MainMusicClockChecks check;
    const auto value=mainMenuSmokeEnvironment("CONCORD_MAIN_MENU_MUSIC_CLOCK_OUTPUT");
    if(!value||value->empty()){std::cerr<<"Main music clock requires explicit isolated output\n";return 1;}
    const fs::path directory(*value);
    const auto projectRoot=fs::current_path();
    const auto allowedRoot=(projectRoot/L"build"/L"smoke"/L"main-menu-music-clock").lexically_normal().native()+L"\\";
    if(!fs::is_regular_file(projectRoot/L"CMakeLists.txt")||!directory.is_absolute()||!directory.lexically_normal().native().starts_with(allowedRoot)||fs::exists(directory)){std::cerr<<"run from the project root and use a new build/smoke/main-menu-music-clock output directory\n";return 1;}
    fs::create_directories(directory);const auto previous=fs::current_path();
    struct RestoreDirectory {fs::path path;~RestoreDirectory(){std::error_code error;fs::current_path(path,error);}} restore{previous};fs::current_path(directory);
    const auto writeWave=[](const fs::path& file){
        std::ofstream output(file,std::ios::binary);
        const auto u16=[&](std::uint16_t x){for(int i=0;i<2;++i)output.put(static_cast<char>(x>>(i*8)));};
        const auto u32=[&](std::uint32_t x){for(int i=0;i<4;++i)output.put(static_cast<char>(x>>(i*8)));};
        output.write("RIFF",4);u32(32036);output.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(8000);u32(16000);u16(2);u16(16);output.write("data",4);u32(32000);const std::array<char,32000> silence{};output.write(silence.data(),static_cast<std::streamsize>(silence.size()));return bool(output);
    };
    if(!writeWave("a.wav")||!writeWave("b.wav")){std::cerr<<"Main music PCM write failed\n";return 1;}
    {std::ofstream bad("bad.wav",std::ios::binary);bad<<"invalid wave";if(!bad){std::cerr<<"Main bad PCM write failed\n";return 1;}}
    {MenuAudio defaults;check.expect(defaults.clock==&runtimeWin32UiClock(),"immutable native provider is the production default");}
    const auto current=runMainMusicClockScene<MenuAudio>(check);const auto original=runMainMusicClockScene<OriginalMainMenuAudio>(check);
    check.expect(current.states==original.states,"complete local music/cache fields and sampling states equal to whole old class");
    check.expect(current.reads==original.reads&&current.reads.size()==14,"every independent source value and read order equal to old class");
    const auto write=[&](const fs::path& file,const MainMusicClockObservation& x){std::ofstream out(file,std::ios::binary);for(const auto& state:x.states)out<<state<<'\n';out<<"reads=";for(auto read:x.reads)out<<read<<',';out<<'\n';return bool(out);};
    check.expect(write("current.txt",current)&&write("original.txt",original),"full original/current observations retained");
    std::cout<<"Main menu music clock: checks="<<check.checks<<" failures="<<check.failures<<" skipped=0 states="<<current.states.size()
        <<" sourceReads="<<current.reads.size()<<','<<original.reads.size()<<" originalClass=complete globalFakeClock=0 nativePhases=2 positions="<<current.position<<','<<original.position
        <<" finalAlias=closed hearing=not-run loopback=not-run\n";
    return check.failures?1:0;
}