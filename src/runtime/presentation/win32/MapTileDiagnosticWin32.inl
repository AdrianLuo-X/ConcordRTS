// Explicit diagnostics only. Timing excludes reference verification and file IO.
// No normal GPU readback is added. Captured commands contain numeric values only.
{
    std::array<wchar_t,4096> directory{};
    const auto length=GetEnvironmentVariableW(L"CONCORD_S43_TILE_TRACE",directory.data(),static_cast<DWORD>(directory.size()));
    if(length) {
        if(length>=directory.size())return E_INVALIDARG;
        const auto output=std::filesystem::path(directory.data()).lexically_normal();
        const auto pathText=output.wstring();
        const auto projectRoot=std::filesystem::current_path();
        const auto allowedRoot=(projectRoot/L"build"/L"smoke"/L"frame-preparation").lexically_normal().wstring()+L"\\";
        if(!std::filesystem::is_regular_file(projectRoot/L"CMakeLists.txt")
            ||!std::filesystem::is_regular_file(projectRoot/L"src/runtime/host/RuntimeShell.cpp")
            ||!pathText.starts_with(allowedRoot)||!std::filesystem::is_directory(output))return E_INVALIDARG;
        const bool verify=GetEnvironmentVariableW(L"CONCORD_S43_TILE_VERIFY",nullptr,0)!=0;
        if(verify&&indices!=concord::render::referenceMapTiles(concord::render::snapshotMapTiles(f,deviceGeneration)))return E_FAIL;
        const auto trace=output/L"tiles.tsv";const bool header=!std::filesystem::exists(trace);
        std::ofstream stream(trace,std::ios::app);
        if(header)stream<<"sequence\ttick\twidth\theight\tcommands\twords\tworkers\tpeakRunning\tsnapshotMs\tdispatchMs\twaitMs\tmergeMs\ttotalMs\tsnapshotBytes\townedPeakBytes\tverified\n";
        const auto& m=prepared.metrics;
        stream<<f.sequence<<'\t'<<f.tick<<'\t'<<f.width<<'\t'<<f.height<<'\t'<<list.commands().size()<<'\t'<<indices.size()<<'\t'
            <<tilePreparer.stats().workerThreads<<'\t'<<m.peakRunning<<'\t'<<m.snapshotMs<<'\t'<<m.dispatchMs<<'\t'<<m.waitMs<<'\t'<<m.mergeMs<<'\t'<<m.totalMs<<'\t'<<m.snapshotBytes<<'\t'<<m.ownedPeakBytes<<'\t'<<verify<<'\n';
        if(!stream)return E_FAIL;
        const auto capture=output/(L"frame-"+std::to_wstring(f.sequence)+L".commands");
        if(!std::filesystem::exists(capture)) {
            std::ofstream file(capture,std::ios::binary);
            const std::array<std::uint32_t,3> dimensions={static_cast<std::uint32_t>(f.width),static_cast<std::uint32_t>(f.height),static_cast<std::uint32_t>(list.commands().size())};
            file.write(reinterpret_cast<const char*>(dimensions.data()),sizeof(dimensions));
            file.write(reinterpret_cast<const char*>(list.commands().data()),static_cast<std::streamsize>(list.commands().size()*sizeof(concord::render::MapDrawCommand)));
            if(!file)return E_FAIL;
        }
    }
}
