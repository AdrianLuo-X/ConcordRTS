// Private diagnostic only: real resources, before first native DIB.
bool runMemoryOnlyShellFrameSmoke(const RuntimeConfig& config) {
        MenuState memoryState;
        concord::ui::MenuPixelBuffer memoryFrame;
        if(!initializeState(memoryState,config,true)
            || memoryState.backBuffer.dc()!=nullptr || memoryState.backBuffer.bitmap()!=nullptr || memoryState.backBuffer.pixels()!=nullptr
            || !memoryFrame.reset(kBaseWidth,kBaseHeight)
            || !composeShellPixels(memoryState,memoryFrame.view())) {
            std::cerr << "W018 actual-resource no-DIB production composition failed.\n"; return false;
        }
        HDC memoryReference=GetDC(nullptr);
        const bool nativeMatches=memoryReference!=nullptr
            && ensureShellBackBuffer(memoryState,memoryReference)
            && renderShellBackBuffer(memoryState) && memoryFrame.bytes()==memoryState.composeFrame.bytes();
        if(memoryReference) ReleaseDC(nullptr,memoryReference);
        if(!nativeMatches) {
            std::cerr << "W018 complete no-DIB/native same-state frame differs.\n"; return false;
        }
        std::cout << "W018 memory-only frame: bytes=" << memoryFrame.bytes().size()
            << " noNativeBackBufferBefore=yes fullFrame=exact\n";
    return true;
}
