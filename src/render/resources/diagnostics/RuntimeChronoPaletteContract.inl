bool runRuntimeChronoPaletteContractCheck(const RuntimeMapRenderContext& context, std::string& error) {
    auto cache=std::make_unique<RuntimeMapRenderCache>();
    constexpr int panel=160,width=panel*3,height=panel*4;
    std::vector<std::uint32_t> pixels(width*height,0x00242c30);
    auto probe=context; probe.vision=nullptr; probe.surfacePixels=pixels.data();
    probe.surfaceWidth=width; probe.surfaceHeight=height;
    RuntimeMapCamera camera; camera.initialized=true;
    const std::array<std::string,4> images={"WARPIN","WARPOUT","WARPAWAY","CHRONOSK"};
    for (int row=0;row<4;++row) {
        const auto* frame=loadRuntimeAuxiliaryShpSpriteFromCache(images[row],0,probe,*cache,false);
        if (!frame) { error="chrono palette: missing "+images[row];return false; }
        const int index=std::min(4,frame->frameCount-1);
        std::string chosen;
        const auto* automatic=runtimePaletteForAuxiliaryEffectSprite(probe,*cache,images[row],RuntimeAuxiliaryEffectKind::Impact,&chosen);
        const std::array<const std::vector<RuntimePaletteColor>*,3> palettes={automatic,
            runtimeAnimPaletteForRender(probe,*cache), runtimeUnitPaletteForRender(probe,*cache,false)};
        std::cout << "Chrono palette " << images[row] << ": " << chosen << '\n';
        for (int col=0;col<3;++col) {
            const RECT view{col*panel,row*panel,(col+1)*panel,(row+1)*panel};
            const POINT center{col*panel+panel/2,row*panel+panel/2};
            if (!palettes[col] || !drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr,view,center,
                probe,camera,*cache,images[row],index,*palettes[col],0,0,false,false,RGB(255,255,255),255,true)) {
                error="chrono palette: draw failed "+images[row];return false;
            }
        }
        int blue=0,red=0,visible=0;
        for (int y=row*panel;y<(row+1)*panel;++y) for (int x=0;x<panel;++x) {
            const auto automaticPixel=pixels[y*width+x];
            if (automaticPixel!=pixels[y*width+x+panel]) {
                error="chrono palette: automatic palette differs from original anim.pal";return false;
            }
            if (automaticPixel==0x00242c30) continue;
            ++visible;
            const int r=(automaticPixel>>16)&255,g=(automaticPixel>>8)&255,b=automaticPixel&255;
            blue+=b>r+20;
            red+=r>b+20 && r>g+20;
        }
        if (chosen!="AnimPalette" || visible<10
            || ((row==1 || row==3) && (blue<10 || red>0))
            || (row==0 && red<10)) {
            error="chrono palette: original blue teleport/sparkle or red reinforcement colors changed";return false;
        }
        std::cout << "Chrono original colors " << images[row] << ": blue=" << blue << " red=" << red << '\n';
    }
    if (const char* dir=std::getenv("CONCORD_CHRONO_EFFECTS_DIR"); dir && *dir)
        if (!writeRuntimeBgraBmp(std::string(dir)+"/chrono-palettes.bmp",width,height,pixels)) {
            error="chrono palette preview write";return false;
        }
    return true;
}
