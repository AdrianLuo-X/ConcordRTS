#include <cstdint>

// Explicitly enabled observer delegates to the same production native owner.
// No mutable global, fake message source or alternate simulation.
class RuntimeHostLoopDiagnosticPort final : public RuntimeHostLoopPort {
public:
    explicit RuntimeHostLoopDiagnosticPort(RuntimeHostLoopPort& native) noexcept : native_(native) {}
    bool drainMessages() override {
        ++drains_;
        const bool running = native_.drainMessages();
        if (!running) ++quitReturns_;
        return running;
    }
    void sleepForIdleSlice() override {
        ++sleeps_;
        native_.sleepForIdleSlice();
    }
    void observeBudget(bool multiplayer, int limit) noexcept {
        if (multiplayer) ++multiBudgets_; else ++singleBudgets_;
        lastBudget_ = limit;
    }
    void write(std::ostream& output) const {
        output << "Host loop native services: drains=" << drains_ << " sleeps=" << sleeps_
            << " quitReturns=" << quitReturns_ << " singleBudgets=" << singleBudgets_
            << " multiBudgets=" << multiBudgets_ << " lastBudget=" << lastBudget_
            << " currentThreadQueue=1 nativeOwner=W013 sleepRequestMs=1 globalFakeContext=0\n";
    }
private:
    RuntimeHostLoopPort& native_;
    std::uint64_t drains_ = 0, sleeps_ = 0, quitReturns_ = 0;
    std::uint64_t singleBudgets_ = 0, multiBudgets_ = 0;
    int lastBudget_ = 0;
};
