#pragma once

#include <string>

// A synchronous candidate transaction. The adapter owns its candidate until
// execution returns; commit publishes it without throwing. Device rehydration
// follows that publication and cannot turn a committed load into a failure.
class RuntimeLoadTransaction {
public:
    virtual ~RuntimeLoadTransaction() = default;
    virtual void seedCandidate(bool forceVisionEnabled) = 0;
    virtual bool materializeCandidate(const std::string& savePath, std::string& error) = 0;
    virtual void prepareCandidate() = 0;
    virtual void commitCandidate() noexcept = 0;
    virtual void presentCommitted() = 0;
    // Destroy actions, then candidate, before an exception handler observes
    // failure; this also runs on normal returns and materializer rejection.
    virtual void releaseCandidate() noexcept = 0;
};

bool executeRuntimeLoadTransaction(RuntimeLoadTransaction& transaction,
    const std::string& savePath, std::string& error, bool forceVisionEnabled);
