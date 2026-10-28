    void observeMap(HDC dc,int w,int h,bool reused,bool submitted,const concord::render::MapDrawFrame& frame) override {
        if(!frame.valid()||!gpu||gpu->uploadedPixels.size()!=static_cast<std::size_t>(w)*h*4){error="actual complete GPU map diagnostic image missing";return;}
        // Explicit diagnostic readback only. Production never publishes the partial world DIB as complete.
        observe(dc,w,h,reused,submitted,{gpu->uploadedPixels,w,h,static_cast<std::size_t>(w)*4,frame.sequence});
    }
