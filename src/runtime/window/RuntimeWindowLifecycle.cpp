#include "runtime/window/RuntimeWindowLifecycle.h"

bool handleRuntimeWindowLifecycle(RuntimeWindowLifecycleEvent event,
    bool sessionAttached, RuntimeWindowLifecyclePort& port) {
    switch (event) {
    case RuntimeWindowLifecycleEvent::Hidden:
    case RuntimeWindowLifecycleEvent::Minimized:
        if (sessionAttached) port.resetFrameMeter();
        return false;
    case RuntimeWindowLifecycleEvent::Close:
        if (sessionAttached) port.reportDeparture();
        port.destroyWindow();
        return true;
    case RuntimeWindowLifecycleEvent::Destroyed:
        if (sessionAttached) {
            port.stopEva();
            port.stopMusic();
            port.releaseGraphics();
        }
        port.postQuit();
        return true;
    }
    return false;
}
