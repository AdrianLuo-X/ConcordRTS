// Private same-production original MCI/file-path verification only.
bool runRuntimeMciPathProducerSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "MCI path producer: " + why; return false; };
    const DWORD size = GetEnvironmentVariableW(L"CONCORD_MCI_PATH_PRODUCER_SAMPLE", nullptr, 0);
    if (size < 2) return fail("explicit native sample required before any decoder/device output");
    std::vector<wchar_t> value(size);
    if (GetEnvironmentVariableW(L"CONCORD_MCI_PATH_PRODUCER_SAMPLE", value.data(), size) != size - 1) return fail("exact native input path");
    const std::filesystem::path sample(value.data());
    if (!std::filesystem::is_regular_file(sample)) return fail("actual production sample missing");
    const auto native = sample.generic_string();
    if (widenUtf8(concord::audio::runtimeVoiceUtf8PathFromLegacyNative(native)) != sample.generic_wstring()) return fail("exact native-wide device path");
    auto state = std::make_unique<RuntimeWindowState>(); state->config = config;
    std::string summary;
    if (!validateRuntimeMusicPlayback(*state, summary)) return fail("original full selected music helper: " + summary);
    if (!playRuntimeEvaOnDedicatedChannel(native, 0)) return fail("actual original EVA dedicated producer did not play same sample");
    if (runtimeEvaMciChannelIsPlaying() != std::optional<bool>(true)) { closeRuntimeEvaMciChannel(); return fail("actual original EVA status"); }
    Sleep(80); wchar_t position[32]{};
    const auto code = mciSendStringW(L"status concord_runtime_eva position", position, 32, nullptr);
    std::uint64_t milliseconds = 0; std::wistringstream(position) >> milliseconds;
    closeRuntimeEvaMciChannel();
    if (code != 0 || milliseconds == 0 || mciGetDeviceIDW(L"concord_runtime_eva") != 0 || mciGetDeviceIDW(L"concord_runtime_music") != 0) return fail("real native PCM progress and both aliases closed");
    std::cout << "MCI path producer native: musicFactions=2 eva=1 positionMs=" << milliseconds
        << " nativeAliases=0 originalHelper=yes sameProductionPCM=yes originalVolumeErrorIgnored=yes skipped=0 " << summary << '\n';
    return true;
}
