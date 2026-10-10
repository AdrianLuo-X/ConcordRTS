#include "core/io/RuntimeAtomicFile.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Runtime atomic file smoke failure: " << message << '\n';
    }
    return condition;
}

std::uint64_t processId() {
#ifdef _WIN32
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

std::string temporaryLeafName(std::uint64_t serial) {
    std::ostringstream name;
    name << ".concord-atomic-" << std::hex << std::setfill('0')
         << std::setw(8) << processId()
         << '-' << std::setw(16) << serial << ".tmp";
    return name.str();
}

bool writeDirectly(
    const std::filesystem::path& path,
    const std::string& text
) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    return output.good();
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

bool atomicWriteText(
    const std::filesystem::path& path,
    const std::string& text,
    std::string& error
) {
    return runtimeWriteFileAtomically(
        path,
        [&text](std::ostream& output, std::string&) {
            output.write(
                text.data(),
                static_cast<std::streamsize>(text.size())
            );
            return output.good();
        },
        error
    );
}

std::filesystem::path createUniqueRoot(bool& success) {
    std::error_code error;
    const std::filesystem::path systemTemporary =
        std::filesystem::temp_directory_path(error);
    if (error) {
        success &= expect(false, "could not locate the system temp directory");
        return {};
    }
    const auto timestamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    for (std::uint64_t attempt = 0; attempt < 64; ++attempt) {
        std::ostringstream leaf;
        leaf << "concord-atomic-smoke-" << std::hex << processId()
             << '-' << timestamp << '-' << attempt;
        const std::filesystem::path candidate = systemTemporary / leaf.str();
        error.clear();
        if (std::filesystem::create_directory(candidate, error)) {
            return candidate;
        }
        if (error) {
            success &= expect(
                false,
                "could not create isolated smoke directory: "
                    + error.message()
            );
            return {};
        }
    }
    success &= expect(false, "unique smoke-directory attempts were exhausted");
    return {};
}

bool testExclusiveCollisionAndFirstWrite(
    const std::filesystem::path& root,
    std::filesystem::path& collisionPath,
    std::filesystem::path& targetPath
) {
    collisionPath = root / temporaryLeafName(1);
    targetPath = root / "first-save.dat";
    const std::string decoy = "this pre-existing collision is not ours";
    bool success = expect(
        writeDirectly(collisionPath, decoy),
        "could not create deterministic temporary-name collision"
    );
    std::string error;
    success &= expect(
        atomicWriteText(targetPath, "first atomic payload", error),
        "first atomic write failed: " + error
    );
    success &= expect(
        readText(targetPath) == "first atomic payload",
        "first atomic write produced the wrong bytes"
    );
    success &= expect(
        readText(collisionPath) == decoy,
        "exclusive-name retry modified or deleted the colliding file"
    );
    return success;
}

bool testReplacement(
    const std::filesystem::path& root,
    std::filesystem::path& targetPath
) {
    targetPath = root / "replacement.dat";
    std::string error;
    bool success = expect(
        atomicWriteText(targetPath, "old", error),
        "replacement setup write failed: " + error
    );
    error.clear();
    success &= expect(
        atomicWriteText(targetPath, "new replacement payload", error),
        "atomic replacement failed: " + error
    );
    success &= expect(
        readText(targetPath) == "new replacement payload",
        "atomic replacement did not publish the complete new payload"
    );
    return success;
}

struct WriterFailureTargets {
    std::filesystem::path rejected;
    std::filesystem::path threw;
    std::filesystem::path badStream;
    std::filesystem::path bounded;
};

bool testWriterFailuresPreserveOriginal(
    const std::filesystem::path& root,
    WriterFailureTargets& targetPaths
) {
    bool success = true;
    const std::string original = "irreplaceable original";

    targetPaths.rejected = root / "writer-rejected.dat";
    const std::filesystem::path& rejected = targetPaths.rejected;
    std::string error;
    success &= expect(
        atomicWriteText(rejected, original, error),
        "writer-rejection setup failed: " + error
    );
    const bool rejectedResult = runtimeWriteFileAtomically(
        rejected,
        [](std::ostream& output, std::string& writerError) {
            output << "partial rejected replacement";
            writerError = "intentional writer rejection";
            return false;
        },
        error
    );
    success &= expect(
        !rejectedResult
            && error.find("intentional writer rejection")
                != std::string::npos
            && readText(rejected) == original,
        "writer rejection changed the original or lost its diagnostic"
    );

    targetPaths.threw = root / "writer-threw.dat";
    const std::filesystem::path& threw = targetPaths.threw;
    error.clear();
    success &= expect(
        atomicWriteText(threw, original, error),
        "writer-exception setup failed: " + error
    );
    const bool threwResult = runtimeWriteFileAtomically(
        threw,
        [](std::ostream& output, std::string&) -> bool {
            output << "partial exceptional replacement";
            throw std::runtime_error("intentional writer exception");
        },
        error
    );
    success &= expect(
        !threwResult
            && error.find("intentional writer exception")
                != std::string::npos
            && readText(threw) == original,
        "writer exception changed the original or escaped translation"
    );

    targetPaths.badStream = root / "writer-bad-stream.dat";
    const std::filesystem::path& badStream = targetPaths.badStream;
    error.clear();
    success &= expect(
        atomicWriteText(badStream, original, error),
        "bad-stream setup failed: " + error
    );
    const bool badStreamResult = runtimeWriteFileAtomically(
        badStream,
        [](std::ostream& output, std::string&) {
            output << "partial stream-failure replacement";
            output.setstate(std::ios::badbit);
            return true;
        },
        error
    );
    success &= expect(
        !badStreamResult
            && error.find("flushing") != std::string::npos
            && readText(badStream) == original,
        "stream failure changed the original or was accepted"
    );

    targetPaths.bounded = root / "bounded-error.dat";
    const std::filesystem::path& bounded = targetPaths.bounded;
    error.clear();
    success &= expect(
        atomicWriteText(bounded, original, error),
        "bounded-error setup failed: " + error
    );
    const bool boundedResult = runtimeWriteFileAtomically(
        bounded,
        [](std::ostream&, std::string& writerError) {
            writerError.assign(
                runtimeAtomicFileMaximumErrorBytes * 4U,
                'x'
            );
            return false;
        },
        error
    );
    success &= expect(
        !boundedResult
            && error.size() <= runtimeAtomicFileMaximumErrorBytes
            && readText(bounded) == original,
        "writer diagnostic was not bounded or changed the original"
    );
    return success;
}

bool testInputValidation(
    const std::filesystem::path& root,
    std::filesystem::path& directoryTarget
) {
    bool success = true;
    std::string error;
    success &= expect(
        !atomicWriteText({}, "unused", error)
            && error.find("empty") != std::string::npos,
        "empty target path was not rejected explicitly"
    );

    error.clear();
    success &= expect(
        !atomicWriteText(root / "missing-parent" / "save.dat", "unused", error)
            && error.find("does not exist") != std::string::npos,
        "missing parent directory was not rejected explicitly"
    );

    directoryTarget = root / "directory-target";
    std::error_code fileError;
    success &= expect(
        std::filesystem::create_directory(directoryTarget, fileError),
        "could not create directory target fixture"
    );
    error.clear();
    success &= expect(
        !atomicWriteText(directoryTarget, "unused", error)
            && error.find("directory") != std::string::npos
            && std::filesystem::is_directory(directoryTarget),
        "directory target was overwritten or not rejected"
    );

    const std::string oversizedLeaf(33000, 'q');
    error.clear();
    success &= expect(
        !atomicWriteText(root / oversizedLeaf, "unused", error)
            && error.find("too long") != std::string::npos,
        "oversized path was not rejected explicitly"
    );
    return success;
}

bool testUnicodePath(
    const std::filesystem::path& root,
    std::filesystem::path& unicodeDirectory,
    std::filesystem::path& unicodeTarget
) {
    unicodeDirectory = root
        / std::filesystem::path(std::u8string(u8"原子保存-目录"));
    unicodeTarget = unicodeDirectory
        / std::filesystem::path(std::u8string(u8"战役进度-文件.dat"));
    std::error_code errorCode;
    bool success = expect(
        std::filesystem::create_directory(unicodeDirectory, errorCode),
        "could not create Unicode target directory"
    );
    std::string error;
    success &= expect(
        atomicWriteText(unicodeTarget, "Unicode path payload", error),
        "Unicode-path atomic write failed: " + error
    );
    success &= expect(
        readText(unicodeTarget) == "Unicode path payload",
        "Unicode-path atomic write produced the wrong bytes"
    );
    return success;
}

bool testConcurrentWriters(
    const std::filesystem::path& root,
    std::filesystem::path& targetPath
) {
    targetPath = root / "concurrent-save.dat";
    constexpr std::size_t writerCount = 8;
    std::vector<std::string> payloads;
    payloads.reserve(writerCount);
    for (std::size_t index = 0; index < writerCount; ++index) {
        payloads.push_back(
            "concurrent writer " + std::to_string(index) + '\n'
                + std::string(150000, static_cast<char>('A' + index))
        );
    }
    std::vector<int> results(writerCount, 0);
    std::vector<std::string> errors(writerCount);
    std::vector<std::thread> threads;
    threads.reserve(writerCount);
    for (std::size_t index = 0; index < writerCount; ++index) {
        threads.emplace_back([&, index]() {
            results[index] = atomicWriteText(
                targetPath,
                payloads[index],
                errors[index]
            ) ? 1 : -1;
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    bool success = true;
    for (std::size_t index = 0; index < writerCount; ++index) {
        success &= expect(
            results[index] == 1,
            "concurrent writer " + std::to_string(index)
                + " failed: " + errors[index]
        );
    }
    const std::string published = readText(targetPath);
    success &= expect(
        std::find(payloads.begin(), payloads.end(), published)
            != payloads.end(),
        "concurrent publication was torn or did not match one whole writer"
    );
    return success;
}

#ifdef _WIN32
bool testReplaceFailurePreservesOriginal(
    const std::filesystem::path& root,
    std::filesystem::path& targetPath
) {
    targetPath = root / "locked-replacement.dat";
    const std::string original = "locked original";
    std::string error;
    bool success = expect(
        atomicWriteText(targetPath, original, error),
        "locked-target setup failed: " + error
    );
    HANDLE lock = CreateFileW(
        targetPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    success &= expect(
        lock != INVALID_HANDLE_VALUE,
        "could not lock replacement target"
    );
    if (lock == INVALID_HANDLE_VALUE) {
        return false;
    }
    error.clear();
    const bool replaced = atomicWriteText(targetPath, "must not publish", error);
    CloseHandle(lock);
    success &= expect(
        !replaced
            && error.find("replacing") != std::string::npos
            && readText(targetPath) == original,
        "replace failure changed the locked original or lacked a diagnostic"
    );
    return success;
}
#endif

bool expectNoUnexpectedTemporaryFiles(
    const std::filesystem::path& directory,
    const std::filesystem::path& allowedCollision
) {
    std::error_code error;
    bool success = true;
    for (std::filesystem::directory_iterator iterator(directory, error), end;
         !error && iterator != end;
         iterator.increment(error)) {
        const std::filesystem::path& path = iterator->path();
        const std::string leaf = path.filename().string();
        if (leaf.rfind(".concord-atomic-", 0) == 0
            && path != allowedCollision) {
            success &= expect(
                false,
                "unexpected temporary file remained after save: " + leaf
            );
        }
    }
    success &= expect(!error, "could not inspect temporary-file cleanup");
    return success;
}

bool removeKnownPath(const std::filesystem::path& path, bool directory) {
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    return expect(
        removed && !error,
        std::string{"could not remove known smoke "}
            + (directory ? "directory" : "file") + ": "
            + path.filename().string()
            + (error ? " (" + error.message() + ')' : "")
    );
}

} // namespace

int main() {
    bool success = true;
    const std::filesystem::path root = createUniqueRoot(success);
    if (root.empty()) {
        return 1;
    }

    std::filesystem::path collision;
    std::filesystem::path firstTarget;
    std::filesystem::path replacementTarget;
    WriterFailureTargets failureTargets;
    std::filesystem::path directoryTarget;
    std::filesystem::path unicodeDirectory;
    std::filesystem::path unicodeTarget;
    std::filesystem::path concurrentTarget;
#ifdef _WIN32
    std::filesystem::path lockedTarget;
#endif

    success &= testExclusiveCollisionAndFirstWrite(
        root,
        collision,
        firstTarget
    );
    success &= testReplacement(root, replacementTarget);
    success &= testWriterFailuresPreserveOriginal(root, failureTargets);
    success &= testInputValidation(root, directoryTarget);
    success &= testUnicodePath(root, unicodeDirectory, unicodeTarget);
    success &= testConcurrentWriters(root, concurrentTarget);
#ifdef _WIN32
    success &= testReplaceFailurePreservesOriginal(root, lockedTarget);
#endif
    success &= expectNoUnexpectedTemporaryFiles(root, collision);
    success &= expectNoUnexpectedTemporaryFiles(unicodeDirectory, {});

    success &= removeKnownPath(collision, false);
    success &= removeKnownPath(firstTarget, false);
    success &= removeKnownPath(replacementTarget, false);
    success &= removeKnownPath(failureTargets.rejected, false);
    success &= removeKnownPath(failureTargets.threw, false);
    success &= removeKnownPath(failureTargets.badStream, false);
    success &= removeKnownPath(failureTargets.bounded, false);
    success &= removeKnownPath(directoryTarget, true);
    success &= removeKnownPath(unicodeTarget, false);
    success &= removeKnownPath(unicodeDirectory, true);
    success &= removeKnownPath(concurrentTarget, false);
#ifdef _WIN32
    success &= removeKnownPath(lockedTarget, false);
#endif
    success &= removeKnownPath(root, true);

    if (!success) {
        return 1;
    }
    std::cout << "Runtime atomic file smoke passed\n";
    return 0;
}
