#include "runtime/persistence/RuntimeLoadTransaction.h"

#include <exception>

bool executeRuntimeLoadTransaction(RuntimeLoadTransaction& transaction,
    const std::string& savePath, std::string& errorMessage, bool forceVisionEnabled) {
    try {
        struct CandidateLifetime {
            RuntimeLoadTransaction& transaction;
            ~CandidateLifetime() { transaction.releaseCandidate(); }
        } candidateLifetime{transaction};
        transaction.seedCandidate(forceVisionEnabled);
        if (!transaction.materializeCandidate(savePath, errorMessage)) {
            return false;
        }
        transaction.prepareCandidate();
        transaction.commitCandidate();
        try {
            transaction.presentCommitted();
        } catch (...) {
        }
        return true;
    } catch (const std::exception& exception) {
        try {
            errorMessage = std::string("load materialization failed: ")
                + exception.what();
        } catch (...) {
        }
        return false;
    } catch (...) {
        try {
            errorMessage = "load materialization failed";
        } catch (...) {
        }
        return false;
    }
}
