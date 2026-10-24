#include "ui/audio/RuntimeSkirmishMusicPolicy.h"
namespace concord::ui {
void SkirmishMusicPolicy::start(const std::filesystem::path& path,bool enabled,
    concord::audio::MenuMusicDevice& device) {
    if(!enabled || path.empty() || !probe_ || !probe_(path)) return;
    if(open_ && path_==path) return;
    if(open_) stop(device);
    path_=path;
    next_=clock_.nowMs()+1000ULL;
    // The device's open retains the original native pre-open reset/close.
    const bool opened=device.open(path);
    const bool played=opened ? device.playFromStart() : false;
    if(opened && played) open_=true;
    else {device.close();open_=false;}
}
void SkirmishMusicPolicy::poll(bool enabled,concord::audio::MenuMusicDevice& device) {
    if(!enabled || path_.empty()) return;
    const RuntimeUiTimeMs now=clock_.nowMs();
    if(now<next_) return;
    next_=now+1000ULL;
    if(!open_) {
        const auto requested=path_;start(requested,enabled,device);return;
    }
    const auto playing=device.isPlaying();
    if(playing && *playing) return;
    if(device.playFromStart()) return;
    const auto requested=path_;
    device.close();open_=false;
    start(requested,enabled,device);
}
void SkirmishMusicPolicy::stop(concord::audio::MenuMusicDevice& device) {
    device.stop();device.close();open_=false;path_.clear();next_=0;
}
} // namespace concord::ui
