#pragma once

// Single UI-thread window lifetime. Effects are synchronous, including a
// destroy request reentering this controller with Destroyed before returning.
enum class RuntimeWindowLifecycleEvent { Hidden, Minimized, Close, Destroyed };
enum class RuntimeWindowCreateResult { Created, RegistrationFailed, CreationFailed };
class RuntimeWindowLifecyclePort {
public:
    virtual ~RuntimeWindowLifecyclePort() = default;
    virtual void resetFrameMeter() = 0;
    virtual void reportDeparture() = 0;
    virtual void destroyWindow() = 0;
    virtual void stopEva() = 0;
    virtual void stopMusic() = 0;
    virtual void releaseGraphics() = 0;
    virtual void postQuit() = 0;
};
// true means the native adapter consumes this message. Hidden/minimized still
// take the native default path after the local frame meter has been reset.
bool handleRuntimeWindowLifecycle(RuntimeWindowLifecycleEvent event,
    bool sessionAttached, RuntimeWindowLifecyclePort& port);
