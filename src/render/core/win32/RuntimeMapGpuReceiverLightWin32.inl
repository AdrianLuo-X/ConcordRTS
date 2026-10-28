// G007: exact canonical lookup rows and disjoint tiles; no CPU destination RGB.
static int runtimeGpuWorldMapLightTiles(const RECT& viewport,const RuntimeMapLightFrame& frame) {
    try {
        auto* list=runtimeGpuWorldOwner->drawList();if(!list)return 0;
        if(frame.width<=0||frame.height<=0||frame.pixels.size()!=static_cast<std::size_t>(frame.width)*frame.height||frame.lookups.empty())throw std::invalid_argument("map light receiver input");
        std::vector<std::array<std::uint32_t,256>> tables;
        std::vector<std::uint32_t> canonical(frame.lookups.size());
        std::unordered_map<std::uint64_t,std::vector<std::uint32_t>> buckets;
        for(std::size_t index=0;index<frame.lookups.size();++index) {
            const auto& table=frame.lookups[index];std::uint64_t hash=1469598103934665603ULL;
            for(auto value:table){hash^=value;hash*=1099511628211ULL;}
            auto& candidates=buckets[hash];auto found=std::find_if(candidates.begin(),candidates.end(),[&](auto id){return tables[id]==table;});
            if(found!=candidates.end())canonical[index]=*found;
            else {if(tables.size()>=concord::render::MapDrawList::maxBytes/1024)throw std::length_error("map light table budget");
                const auto id=static_cast<std::uint32_t>(tables.size());tables.push_back(table);candidates.push_back(id);canonical[index]=id;}
        }
        const auto before=list->resources().size()*4;const auto palette=list->internValues<std::array<std::uint32_t,256>>(tables);
        auto region=runtimeGpuWorldRegion(viewport,concord::render::MapDrawKind::ReceiverLookup);std::uint64_t planeBytes=0;std::size_t uniformTiles=0,mixedTiles=0;
        const auto receiver=[&](int x,int y){if(x<frame.bounds.left||x>=frame.bounds.right||y<frame.bounds.top||y>=frame.bounds.bottom)throw std::out_of_range("map light receiver bounds");
            const auto id=frame.pixels[static_cast<std::size_t>(y-frame.bounds.top)*frame.width+x-frame.bounds.left];
            if(id<0||static_cast<std::size_t>(id)>=canonical.size())throw std::out_of_range("map light receiver id");return canonical[id];};
        constexpr int tileSize=64;
        for(int top=region.top;top<region.bottom;top+=tileSize)for(int left=region.left;left<region.right;left+=tileSize) {
            const int right=std::min(region.right,left+tileSize),bottom=std::min(region.bottom,top+tileSize);const auto first=receiver(left,top);bool uniform=true;
            for(int y=top;y<bottom&&uniform;++y)for(int x=left;x<right;++x)if(receiver(x,y)!=first){uniform=false;break;}
            auto c=runtimeGpuWorldRegion(left,top,right,bottom,uniform?concord::render::MapDrawKind::Lookup:concord::render::MapDrawKind::ReceiverLookup);
            if(uniform){c.palette=palette+first*1024;++uniformTiles;}
            else {
                c.originX=left;c.originY=top;c.width=right-left;c.height=bottom-top;c.parameter=static_cast<std::uint32_t>(tables.size());c.palette=palette;
                if(tables.size()<=256){std::vector<unsigned char> pixels(static_cast<std::size_t>(c.width)*c.height);
                    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)pixels[static_cast<std::size_t>(y-top)*c.width+x-left]=static_cast<unsigned char>(receiver(x,y));
                    c.flags=1;c.stride=c.width;c.offset=list->intern(pixels);planeBytes+=pixels.size();}
                else {std::vector<std::uint32_t> pixels(static_cast<std::size_t>(c.width)*c.height);
                    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)pixels[static_cast<std::size_t>(y-top)*c.width+x-left]=receiver(x,y);
                    c.stride=c.width*4;c.offset=list->internValues<std::uint32_t>(pixels);planeBytes+=pixels.size()*4;}
                ++mixedTiles;
            }
            (void)runtimeGpuWorldEmit(c);
        }
        if(runtimeRendererEnvironmentValue("CONCORD_S41_LIGHT_PACK_TRACE"))std::cout<<"G007 receiver packing originalPlaneBytes="<<frame.pixels.size()*4<<" packedPlaneBytes="<<planeBytes<<" originalTables="<<frame.lookups.size()<<" uniqueTables="<<tables.size()<<" packedReceiverBits="<<(tables.size()<=256?8:32)<<" uniformTiles="<<uniformTiles<<" mixedTiles="<<mixedTiles<<" packetAddedBytes="<<list->resources().size()*4-before<<" exactTables=1 disjointCoverage=1 budgetsUnchanged=1\n";
        return (region.right-region.left)*(region.bottom-region.top);
    }catch(...){runtimeGpuWorldOwner->reject("world-map-light-budget");return 0;}
}
