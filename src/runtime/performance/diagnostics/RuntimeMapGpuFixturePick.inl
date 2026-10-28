// Original positive CPU authority query over a fixed visible building. No GPU pixels.
bool runtimeMapGpuFixturePositivePick(RuntimeWindowState& state,const RECT& client) {
    const auto viewport=runtimeMapViewport(client);const auto projected=runtimeProjectCellXY(80,80,0,state.camera);
    const int centerX=viewport.left+projected.x-state.camera.worldOffsetX,centerY=viewport.top+projected.y-state.camera.worldOffsetY;
    for(int y=std::max(static_cast<int>(viewport.top),centerY-260);y<std::min(static_cast<int>(viewport.bottom),centerY+40);y+=4)
        for(int x=std::max(static_cast<int>(viewport.left),centerX-100);x<std::min(static_cast<int>(viewport.right),centerX+100);x+=4) {
            const auto* object=runtimeCursorObjectAtPoint(state,viewport,{x,y});if(object&&object->instanceId==1){
                if(!std::getenv("CONCORD_S41_VXL_SCENE"))return true;
                const auto front=runtimeProjectCellXY(82,83,0,state.camera);const int fx=viewport.left+front.x-state.camera.worldOffsetX,fy=viewport.top+front.y-state.camera.worldOffsetY;
                for(int py=std::max(static_cast<int>(viewport.top),fy-80);py<std::min(static_cast<int>(viewport.bottom),fy+40);py+=2)for(int px=std::max(static_cast<int>(viewport.left),fx-80);px<std::min(static_cast<int>(viewport.right),fx+80);px+=2){const auto* selected=runtimeCursorObjectAtPoint(state,viewport,{px,py});if(selected&&selected->instanceId==4)return true;}
                return false;
            }
        }
    return false;
}
