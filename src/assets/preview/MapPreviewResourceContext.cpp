#include "assets/preview/MapPreviewResourceContext.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace concord::assets {
struct MapPreviewResourceSession::Impl {
    MapPreviewResourceContext context;
    std::array<std::any, static_cast<std::size_t>(MapPreviewCacheDomain::Count)> caches;
};
MapPreviewResourceSession::MapPreviewResourceSession(MapPreviewResourceContext context)
    : impl_(std::make_unique<Impl>()) {
    if (context.editionModPriority != MapPreviewEditionModPriority::LegacyConfiguredRootOrder)
        throw std::invalid_argument("unsupported preview edition/mod priority");
    if (context.packageRoot.empty()) context.packageRoot = std::filesystem::current_path().string();
    context.packageRoot = std::filesystem::absolute(context.packageRoot).lexically_normal().string();
    impl_->context = std::move(context);
}
MapPreviewResourceSession::~MapPreviewResourceSession() = default;
MapPreviewResourceSession::MapPreviewResourceSession(MapPreviewResourceSession&&) noexcept = default;
MapPreviewResourceSession& MapPreviewResourceSession::operator=(MapPreviewResourceSession&&) noexcept = default;
const MapPreviewResourceContext& MapPreviewResourceSession::context() const { return impl_->context; }
std::any& MapPreviewResourceSession::domainStorage(MapPreviewCacheDomain domain) {
    const auto index = static_cast<std::size_t>(domain);
    if (index >= impl_->caches.size()) throw std::invalid_argument("invalid preview cache domain");
    return impl_->caches[index];
}
std::size_t MapPreviewResourceSession::cachedDomainCount() const {
    std::size_t count = 0;
    for (const auto& storage : impl_->caches) if (storage.has_value()) ++count;
    return count;
}
void MapPreviewResourceSession::refresh() {
    if (impl_->context.mountGeneration == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("preview mount generation exhausted");
    for (auto& storage : impl_->caches) storage.reset();
    ++impl_->context.mountGeneration;
}
bool MapPreviewResourceSession::readLooseBytes(MapPreviewCacheDomain domain,
    const std::string& path, std::vector<unsigned char>& bytes, std::string& error) {
    if (domain != MapPreviewCacheDomain::OverlayLooseBytes
        && domain != MapPreviewCacheDomain::DiagnosticLooseBytes)
        throw std::invalid_argument("loose bytes require a loose-byte preview domain");
    auto& stored = cache<std::vector<unsigned char>>(domain);
    const auto found = stored.find(path);
    if (found != stored.end()) { bytes = found->second; return true; }
    std::filesystem::path resolved(path);
    if (!resolved.is_absolute()) resolved = std::filesystem::path(context().packageRoot) / resolved;
    std::ifstream input(resolved, std::ios::binary);
    if (!input.is_open()) { error = "failed to open file: " + path; return false; }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) { error = "failed to get file size: " + path; return false; }
    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) { error = "failed to read file: " + path; return false; }
    stored.emplace(path, bytes);
    return true;
}
} // namespace concord::assets