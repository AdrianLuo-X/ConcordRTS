#include "audio/backend/RuntimeVoicePath.h"
#include <array>
#include <iostream>
int main(){
 unsigned checks=0,failures=0;const auto expect=[&](bool ok){++checks;if(!ok)++failures;};
 for(const std::string source:std::array<const char*,12>{"",".","..","cache/voice.wav","folder with spaces/voice.wav","../cache/./voice.wav","C:/cache/voice.wav","C:\\cache\\voice.wav","relative.wav","a//b.wav","a/../b.wav","trailing/"}){
  const auto result=concord::audio::runtimeVoiceUtf8PathFromLegacyNative(source);expect(result==source);expect(std::filesystem::path(std::u8string(result.begin(),result.end()))==std::filesystem::path(source));
 }
 const std::u8string utf8=u8"中文目录/声音.wav";const auto native=std::filesystem::path(utf8).string();const std::string expected(utf8.begin(),utf8.end());expect(concord::audio::runtimeVoiceUtf8PathFromLegacyNative(native)==expected);
 std::cout<<"Voice path value: checks="<<checks<<" failures="<<failures<<" skipped=0 asciiExact=12 nativeChineseRoundTrip=1 filesystemLookup=none\n";return failures?1:0;
}
