"""Build a neutral-free flat lake with four symmetric starts from retail data.

Produces ordinary RA2/YR map data and a standalone YRO map package. It does
not change the engine, original archives, or the random-map generator.
"""
from pathlib import Path
import base64
import json
import struct
import sys
import zlib
from collections import deque

REPO = Path(__file__).resolve().parents[1]
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / 'build/flat_lake_map/candidate'
OUT.mkdir(parents=True, exist_ok=True)
MAP_ID = 'concord_flat_lake'
TITLE = '湖心平原（4）'
SIZE = 240
# Screen coordinates are u=x-y+SIZE, v=x+y-SIZE. Clockwise from the
# top, these starts leave the same distance to the lake and the map edge.
STARTS = [(150, 150), (330, 150), (330, 330), (150, 330)]
LAKE_RADIUS = 64
ORE_CENTERS = [(140, 140), (340, 140), (340, 340), (140, 340)]

def ini(path):
    sections = {}
    current = None
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        line = line.split(';', 1)[0].strip()
        if line.startswith('[') and line.endswith(']'):
            current = sections.setdefault(line[1:-1].lower(), {})
        elif current is not None and '=' in line:
            key, value = line.split('=', 1)
            current[key.strip().lower()] = value.strip()
    return sections

theater = ini(REPO/'local_data/resources/mix_exports/ra2md/localmd/temperatmd.ini')
rules = ini(REPO/'local_data/rulesmd.ini')
bases = {}
offset = 0
for index in range(10000):
    section = theater.get(f'tileset{index:04d}')
    if section is None:
        break
    bases[index] = offset
    offset += int(section.get('tilesinset', 0))
clear = bases[int(theater['general']['cleartile'])]
shore = bases[int(theater['general']['shorepieces'])]
water = bases[int(theater['general']['waterset'])]
# Overlay bytes index the ordered registry, not the numeric INI keys (which
# contain gaps). This matches MapDatabase's retail OverlayPack decoder.
gold = next(i for i, name in enumerate(rules['overlaytypes'].values()) if name.upper().startswith('TIB'))
assert clear == 0 and 0 <= gold < 255

def inside(x, y):
    u, v = x-y+SIZE, x+y-SIZE
    return x > 0 and y > 0 and 0 <= u < SIZE*2 and 1 <= v < SIZE*2

def dry(x, y):
    # One symmetric central lake. All terrain, including the water plane,
    # is at level zero. No ramps, cliffs, islands or trees.
    return (x-SIZE)**2 + (y-SIZE)**2 > LAKE_RADIUS**2

# Original complete 2x2 Shore01..40 templates, as used by RuntimeRandomMap.
shore_shapes = [-1, 6, 30, 0, 14, 8, -1, 32, 22, -1, 24, 38, 16, 34, 36, -1]
cells = {}
for by in range(0, SIZE*2+3, 2):
    for bx in range(0, SIZE*2+3, 2):
        mask = sum(int(dry(bx+dx*2, by+dy*2)) << (dy*2+dx) for dy in range(2) for dx in range(2))
        assert mask not in (6, 9), 'A single convex lake must not create diagonal shoreline crossings'
        variant = ((bx//2)*17 + (by//2)*13) % 2
        for dy in range(2):
            for dx in range(2):
                x, y = bx+dx, by+dy
                if not inside(x, y):
                    continue
                is_shore = mask not in (0, 15)
                land = bool(mask & (1 << (dy*2+dx)))
                tile = shore+shore_shapes[mask]+variant if is_shore else clear if land else water+variant
                cells[x, y] = dict(tile=tile, sub=dy*2+dx if is_shore or not land else 0,
                                   land=land, shore=is_shore)

ore = {}
ore_centers = ORE_CENTERS
ore_counts = []
for cx, cy in ore_centers:
    count = 0
    for y in range(cy-5, cy+6):
        for x in range(cx-5, cx+6):
            if (x-cx)**2+(y-cy)**2 > 25:
                continue
            cell = cells[x,y]
            assert cell['land'] and not cell['shore'] and (x,y) not in ore
            ore[x,y] = 10
            count += 1
    ore_counts.append(count)
assert len(set(ore_counts)) == 1
for x, y in STARTS:
    for dy in range(-4,5):
        for dx in range(-4,5):
            cell = cells[x+dx,y+dy]
            assert cell['tile'] == clear and (x+dx,y+dy) not in ore

def components(points):
    remaining = set(points)
    sizes = []
    while remaining:
        queue = deque([remaining.pop()])
        size = 0
        while queue:
            x, y = queue.popleft()
            size += 1
            for adjacent in [(x-1,y),(x+1,y),(x,y-1),(x,y+1)]:
                if adjacent in remaining:
                    remaining.remove(adjacent)
                    queue.append(adjacent)
        sizes.append(size)
    return sorted(sizes, reverse=True)

land_components = components(p for p,c in cells.items() if c['land'])
water_components = components(p for p,c in cells.items() if not c['land'])
assert len(land_components) == len(water_components) == 1
assert not cells[SIZE,SIZE]['land']

def format5(raw):
    result = bytearray()
    for at in range(0,len(raw),8192):
        part = raw[at:at+8192]
        chunk = bytearray()
        if len(part) <= 238:
            chunk.append(17+len(part))
        else:
            chunk.append(0)
            extra = len(part)-18
            while extra > 255:
                chunk.append(0)
                extra -= 255
            chunk.append(extra)
        chunk.extend(part)
        chunk.extend([17,0,0])
        result.extend(struct.pack('<HH',len(chunk),len(part)))
        result.extend(chunk)
    return result

def format80(raw):
    result = bytearray()
    for at in range(0,len(raw),8192):
        part = raw[at:at+8192]
        chunk = bytearray()
        i = 0
        while i < len(part):
            end = i+1
            while end < len(part) and part[end] == part[i]:
                end += 1
            chunk.append(0xfe)
            chunk.extend(struct.pack('<HB',end-i,part[i]))
            i = end
        chunk.append(0x80)
        result.extend(struct.pack('<HH',len(chunk),len(part)))
        result.extend(chunk)
    return result

def section(name, payload):
    text = base64.b64encode(payload).decode('ascii')
    return '\n['+name+']\n'+''.join(f'{i//70+1}={text[i:i+70]}\n' for i in range(0,len(text),70))

text = f'''; ConcordRTS neutral-free flat lake with four cardinal starts, 2026-10-04
[Basic]
Name={TITLE}
MultiplayerOnly=yes
NewINIFormat=4
Official=no
Percent=0
GameMode=standard

[Map]
Theater=TEMPERATE
Size=0,0,{SIZE},{SIZE}
LocalSize=2,4,{SIZE-4},{SIZE-8}

[Header]
Name={TITLE}
MinPlayer=2
MaxPlayer=4
NumberStartingPoints=4
StartX=0
StartY=0
Width={SIZE*2}
Height={SIZE*2}
'''
for i,(x,y) in enumerate(STARTS):
    text += f'Waypoint{i+1}={x-y+SIZE},{x+y-SIZE}\n'
text += '\n[Waypoints]\n'+''.join(f'{i}={y*1000+x}\n' for i,(x,y) in enumerate(STARTS))
text += '''
[Lighting]
Ambient=1
Red=1
Green=1
Blue=1
Ground=0
Level=0

[SpecialFlags]
TiberiumGrows=yes
TiberiumSpreads=yes
TiberiumExplosive=no

[Structures]
'''
text += '\n[Terrain]\n'
iso = bytearray()
overlay = bytearray([255])*(512*512)
amount = bytearray(512*512)
preview = bytearray(SIZE*2*SIZE*2*3)
for (x,y),c in sorted(cells.items(),key=lambda pair:(pair[0][1],pair[0][0])):
    iso.extend(struct.pack('<HHIBBB',x,y,c['tile'],c['sub'],0,0))
    color = (119,129,73) if c['land'] else (34,94,143)
    if c['shore'] and c['land']:
        color = (177,164,109)
    if (x,y) in ore:
        overlay[y*512+x] = gold
        amount[y*512+x] = ore[x,y]
        color = (232,189,54)
    px,py = x-y+SIZE,x+y-SIZE
    for xx in (px-1,px):
        if 0 <= xx < SIZE*2 and 0 <= py < SIZE*2:
            index = (py*SIZE*2+xx)*3
            preview[index:index+3] = bytes(color)
iso.extend(struct.pack('<I',0))
text += section('IsoMapPack5',format5(iso))
text += section('OverlayPack',format80(overlay))
text += section('OverlayDataPack',format80(amount))
text += f'\n[Preview]\nSize=0,0,{SIZE*2},{SIZE*2}\n'+section('PreviewPack',format5(preview))
map_bytes = text.replace('\n','\r\n').encode('utf-8')
(OUT/(MAP_ID+'.map')).write_bytes(map_bytes)
(OUT/'preview.ppm').write_bytes(f'P6\n{SIZE*2} {SIZE*2}\n255\n'.encode()+preview)

def mix_id(name):
    data = bytearray(name.upper().encode('ascii'))
    full = len(data)//4
    remainder = len(data)%4
    if remainder:
        data.append(remainder)
        data.extend([data[full*4]]*(3-remainder))
    return zlib.crc32(data)

packet = f'[MultiMaps]\n1={MAP_ID}\n\n[{MAP_ID}]\nMinPlayers=2\nMaxPlayers=4\nGameMode=standard\n'.encode()
entries = [(MAP_ID+'.map',map_bytes),(MAP_ID+'.pkt',packet)]
directory = bytearray(struct.pack('<HI',len(entries),sum(len(data) for _,data in entries)))
offset = 0
for name,data in entries:
    directory.extend(struct.pack('<III',mix_id(name),offset,len(data)))
    offset += len(data)
(OUT/(MAP_ID+'.yro')).write_bytes(directory+b''.join(data for _,data in entries))
manifest = dict(name=TITLE, id=MAP_ID, size=[SIZE,SIZE], starts=STARTS,
                startLayout='top-right-bottom-left', lakeRadius=LAKE_RADIUS,
                oreCenters=ore_centers, levelRange=[0,0],
                tileCount=len(cells), landComponents=land_components, waterComponents=water_components,
                shoreCells=sum(c['shore'] for c in cells.values()), orePerPlayer=ore_counts,
                goldOverlayIndex=gold, tiles={'clear':clear,'shore':shore,'water':water},
                cacheFile=f'{MAP_ID}_{mix_id(MAP_ID+".map"):08x}.map',
                galleryCount=0, galleryRows=0, galleryGap=0,
                buildingSelection='none', functionalNeutralTypes=[], gallery=[], excluded=[])
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False),encoding='utf-8')
print(json.dumps({key:value for key,value in manifest.items() if key not in ('gallery','excluded')},ensure_ascii=True))
