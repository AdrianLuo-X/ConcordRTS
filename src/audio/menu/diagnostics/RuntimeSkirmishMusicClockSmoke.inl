// W007-02 selected actual MenuAudio consumer: private silent PCM, real MCI.
// No windows, user configuration, volume settings or unrelated process touched.
int runSkirmishMusicClockSmoke(const fs::path& output) {
    int checks=0,failures=0;
    const auto check=[&](bool valid,const char* text){++checks;if(!valid){++failures;std::cerr<<text<<'\n';}};
    struct Clock final : RuntimeUiClock {
        RuntimeUiTimeMs value=0;
        mutable int calls=0;
        std::vector<RuntimeUiTimeMs> sequence;
        mutable std::size_t index=0;
        RuntimeUiTimeMs nowMs() const noexcept override {
            ++calls;return index<sequence.size()?sequence[index++]:value;
        }
    } clock;
    const auto directory=output.parent_path()/"music-clock";fs::create_directories(directory);
    const auto pcm=directory/(std::getenv("CONCORD_SKIRMISH_MUSIC_UNICODE_FIXTURE")
        ? fs::path(L"静音音乐.wav") : fs::path("silence.wav"));
    const auto second=directory/"second.wav",bad=directory/"invalid.wav";
    const auto writeWave=[](const fs::path& path) {
        std::ofstream out(path,std::ios::binary);
        const auto u16=[&](std::uint16_t value){for(int i=0;i<2;++i)out.put(static_cast<char>(value>>(i*8)));};
        const auto u32=[&](std::uint32_t value){for(int i=0;i<4;++i)out.put(static_cast<char>(value>>(i*8)));};
        out.write("RIFF",4);u32(32036);out.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(8000);u32(16000);u16(2);u16(16);
        out.write("data",4);u32(32000);const std::array<char,32000> silence{};out.write(silence.data(),static_cast<std::streamsize>(silence.size()));return bool(out);
    };
    check(writeWave(pcm)&&writeWave(second),"write owned nonempty silent PCM fixtures");
    {std::ofstream f(bad,std::ios::binary);f<<"invalid wave";check(bool(f),"write owned invalid wave");}
    MenuAudio audio(clock);
    struct Cleanup {MenuAudio& audio;~Cleanup(){audio.stopMusic();}} cleanup{audio};
    const auto mode=[] {
        wchar_t text[32]{};const auto error=mciSendStringW(L"status concord_skirmish_intro mode",text,32,nullptr);
        return error==0?std::wstring(text):std::wstring(L"<error>");
    };
    audio.enabled=false;audio.startMusic(pcm);audio.ensureMusicPlaying();
    check(clock.calls==0&&!audio.music.isOpen()&&audio.music.path().empty(),"disabled start/poll never sample clock");
    audio.enabled=true;audio.startMusic({});audio.startMusic(directory/"missing.wav");
    check(clock.calls==0&&!audio.music.isOpen(),"empty/missing inputs return before source sampling");
    clock.value=100;audio.startMusic(pcm);
    check(clock.calls==1&&audio.music.nextProbe()==1100&&audio.music.isOpen()&&audio.music.path()==pcm,"start uses one exact borrowed source sample");
    check(mode()==L"playing","real MCI acknowledges silent PCM playback");
    Sleep(60);wchar_t position[32]{};const auto positionError=mciSendStringW(L"status concord_skirmish_intro position",position,32,nullptr);
    std::uint64_t played=0;std::wistringstream positionStream(position);positionStream>>played;
    check(positionError==0&&played>0,"real MCI playback position advances (no audible-output claim)");
    const int beforeSame=clock.calls;audio.startMusic(pcm);
    check(clock.calls==beforeSame&&audio.music.nextProbe()==1100,"same open path does not resample or restart");
    check(mciSendStringW(L"stop concord_skirmish_intro",nullptr,0,nullptr)==0&&mode()==L"stopped","own device can be stopped before clock deadline");
    clock.value=1099;audio.ensureMusicPlaying();
    check(clock.calls==beforeSame+1&&audio.music.nextProbe()==1100&&mode()==L"stopped","deadline-minus1 polls source but never restarts device");
    clock.value=1100;audio.ensureMusicPlaying();
    check(clock.calls==beforeSame+2&&audio.music.nextProbe()==2100&&mode()==L"playing","exact deadline restarts original wave from0");
    clock.value=50;audio.ensureMusicPlaying();
    check(audio.music.nextProbe()==2100,"backward source preserves original comparison without clamp");
    const int beforeDisabled=clock.calls;audio.enabled=false;audio.ensureMusicPlaying();audio.startMusic(second);
    check(clock.calls==beforeDisabled&&audio.music.path()==pcm,"disabled retains current path and does not sample");
    audio.enabled=true;
    check(mciSendStringW(L"close concord_skirmish_intro",nullptr,0,nullptr)==0,"force owned device loss for recovery");
    const int beforeRecovery=clock.calls;clock.sequence={2100,2500};clock.index=0;audio.ensureMusicPlaying();
    check(clock.calls==beforeRecovery+2&&clock.index==2&&audio.music.nextProbe()==3500&&audio.music.isOpen(),"poll recovery and nested start preserve two independent source samples");
    check(mode()==L"playing"&&audio.music.path()==pcm,"actual device reopened on retained requested file");
    clock.sequence.clear();clock.index=0;clock.value=5000;const int beforeSwitch=clock.calls;audio.startMusic(second);
    check(clock.calls==beforeSwitch+1&&audio.music.path()==second&&audio.music.nextProbe()==6000&&mode()==L"playing","file switch stops old and samples new start exactly once");
    const int beforeStop=clock.calls;audio.stopMusic();
    check(clock.calls==beforeStop&&!audio.music.isOpen()&&audio.music.path().empty()&&audio.music.nextProbe()==0&&mode()==L"<error>","stop closes actual device and clears due without sampling");
    clock.value=7000;audio.startMusic(bad);
    check(!audio.music.isOpen()&&audio.music.path()==bad&&audio.music.nextProbe()==8000&&mode()==L"<error>","actual bad-file error retains requested path and due");
    clock.value=8000;const int beforeBadRetry=clock.calls;audio.ensureMusicPlaying();
    check(clock.calls==beforeBadRetry+2&&!audio.music.isOpen()&&audio.music.nextProbe()==9000,"failed-open retry samples poll plus start independently");
    clock.value=std::numeric_limits<RuntimeUiTimeMs>::max()-500;audio.startMusic(pcm);
    check(audio.music.isOpen()&&audio.music.nextProbe()==499,"original uint64 +1000 modulo wrap retained, not saturation");
    check(mciSendStringW(L"stop concord_skirmish_intro",nullptr,0,nullptr)==0,"stop owned wave around wrap test");
    clock.value=498;audio.ensureMusicPlaying();check(mode()==L"stopped"&&audio.music.nextProbe()==499,"wrapped deadline-minus1 unchanged");
    clock.value=499;audio.ensureMusicPlaying();check(mode()==L"playing"&&audio.music.nextProbe()==1499,"wrapped deadline exact restart");
    audio.stopMusic();check(mode()==L"<error>"&&!audio.music.isOpen()&&audio.music.nextProbe()==0,"final owned alias closed");
    std::cout<<"Skirmish music clock: checks="<<checks<<" failures="<<failures<<" sourceReads="<<clock.calls
        <<" deadline=exact recoverySamples=2 uint64Wrap=original realMciSilentPcm=yes playedPosition="<<played<<" finalAlias=closed\n";
    return failures?1:0;
}
