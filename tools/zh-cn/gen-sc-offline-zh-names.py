#!/usr/bin/env python3
"""Builds the sc-offline mod's Chinese display-name files from the game's own localization.

usage: gen-sc-offline-zh-names.py <localization-dir> <mod-data-dir>

<localization-dir> holds english/global.ini and chinese_(simplified)/global.ini, extracted
read-only from Data.p4k with p4k-extract.py. Writes into <mod-data-dir>:
  ship_names_zh.txt   class|中文名, for every class in ships.txt
  place_names_zh.txt  key|中文名, for star systems, planets, moons, Lagrange and jump points
Official Chinese names are used where CIG has them; the rest follow the same style
(manufacturer + model). Variants keep their base name and add a label in （）.
"""
import os, re, sys

# Official manufacturer names from chinese_(simplified)/global.ini; Kruger, Mirai and Grey's have none.
MAKER_EN = {
    'Aegis': '神盾', 'Anvil': '铁砧', 'Argo': '阿尔戈', 'Banu': '巴努', 'C.O.': '联合寂土', 'Crusader': '十字军',
    'Drake': '德雷克', 'Esperia': '艾斯普亚', 'Gatac': '伽塔克', 'Greycat': '灰猫', 'GRIN': '灰猫', 'Grey\'s': '格雷',
    'Kruger': '克鲁格', 'MISC': 'MISC', 'Mirai': '米莱', 'Origin': '奥源', 'RSI': 'RSI', 'Tumbril': '坦布瑞',
    'Vanduul': '剜度', 'Aopoa': '奥珀',
}
MAKER_PREFIX = {
    'AEGS': '神盾', 'ANVL': '铁砧', 'ARGO': '阿尔戈', 'BANU': '巴努', 'CNOU': '联合寂土', 'CRUS': '十字军',
    'DRAK': '德雷克', 'ESPR': '艾斯普亚', 'GAMA': '伽塔克', 'GRIN': '灰猫', 'GLSN': '格雷', 'KRIG': '克鲁格',
    'MISC': 'MISC', 'MRAI': '米莱', 'ORIG': '奥源', 'RSI': 'RSI', 'TMBL': '坦布瑞', 'VNCL': '剜度',
    'XIAN': '奥珀', 'XNAA': '奥珀',
}
# English model words without an official translation (longest phrases are matched first).
MODEL = {
    'Idris-M': '伊德里斯-M', 'Idris-P': '伊德里斯-P', 'Redeemer': '救赎者', 'Tiburon': '虎鲨', 'Asgard': '阿斯加德',
    'Gladiator': '剑斗士', 'F8C Lightning': 'F8C 闪电', 'Lightning': '闪电', 'Terrapin Medic': '水龟医疗型',
    'F7A Hornet': 'F7A 大黄蜂', 'F7C Hornet': 'F7C 大黄蜂', 'F7C-M Super Hornet': 'F7C-M 超级大黄蜂',
    'F7C-R Hornet Tracker': 'F7C-R 大黄蜂追踪者', 'F7C-S Hornet Ghost': 'F7C-S 大黄蜂幽灵', 'Hornet': '大黄蜂',
    'CSV-SM': 'CSV-SM', 'MOTH': '飞蛾', 'RAFT': 'RAFT', 'Nomad': '游牧者', 'Intrepid': '无畏',
    'Ares Star Fighter Inferno': '战神星际战斗机 地狱火', 'Ares Star Fighter Ion': '战神星际战斗机 离子',
    'A2 Hercules Starlifter': 'A2 大力神星际运输机', 'C2 Hercules Starlifter': 'C2 大力神星际运输机',
    'Clipper': '快剪', 'Command Module': '指挥模块', 'Cutter Rambler': '切割者 漫游者', 'Cutter Scout': '切割者 侦察型',
    'Dragonfly Star Kitten': '蜻蜓 星猫', 'Dragonfly Yellowjacket': '蜻蜓 黄夹克', 'Golem OX': '魔像 OX', 'Golem': '魔像',
    'Ironclad Assault': '铁甲舰 突击型', 'Ironclad': '铁甲舰', 'Pitbull': '斗牛犬', 'Vulture': '秃鹫',
    'Prowler Utility': '徘徊者 通用型', 'Syulen': '赛伦', 'Tyilui': '蒂伊卢伊', 'Basher': '重锤', 'Shiv': '匕首',
    'MDC': 'MDC', 'MTC': 'MTC', 'L-21 Wolf': 'L-21 狼', 'L-22 Alpha Wolf': 'L-22 阿尔法狼', 'S-65 Stingray': 'S-65 黄貂鱼',
    'Fortune': '幸运', 'Hull B': '货轮B', 'Hull C': '货轮C', 'Prospector': '探矿者', 'Razor EX': '剃刀 EX',
    'Razor LX': '剃刀 LX', 'Razor': '剃刀', 'Starfarer': '星际远航者', 'Starlancer TAC': '星际枪骑兵 TAC',
    'Starlancer MAX': '星际枪骑兵 MAX', 'Starlite': '星光', 'Guardian MX': '守护者 MX', 'Guardian QI': '守护者 QI',
    'Guardian': '守护者', 'Pulse LX': '脉冲 LX', 'Pulse': '脉冲', '890 Jump': '890 跃迁', 'Apollo Medivac': '阿波罗 医疗后送',
    'Apollo Triage': '阿波罗 分诊', 'Aurora Mk II': '极光 Mk II', 'Hermes': '赫尔墨斯', 'Meteor': '流星',
    'Polaris': '北极星', 'Salvation': '救赎', 'Zeus Mk II CL': '宙斯 Mk II CL', 'Zeus Mk II ES': '宙斯 Mk II ES',
    'Storm AA': '风暴 防空型', 'Mauler Destroyer': '重殴者驱逐舰', 'Stinger': '毒刺', 'Bengal Carrier': '孟加拉航母',
    'Reclaimer': '回收者', 'Gladius Dunlevy': '角斗士 邓利维', 'Avenger Titan Renegade': '复仇者泰坦 叛逆者',
    'Sabre Firebird': '军刀 火鸟', 'Sabre Peregrine': '军刀 游隼', 'Sabre Raven EX': '军刀 渡鸦 EX',
    'Mk II': 'Mk II', 'Mk I': 'Mk I',
}
# Words that follow an officially translated base (e.g. "Aegis Sabre" + " Firebird").
WORDS = {
    'Renegade': '叛逆者', 'Dunlevy': '邓利维', 'Firebird': '火鸟', 'Peregrine': '游隼', 'Raven': '渡鸦', 'Medic': '医疗型',
    'Executive Edition': '行政版', 'Wikelo War Special': '维克洛战争特别版', 'Wikelo Work Special': '维克洛工作特别版',
    'Wikelo Sneak Special': '维克洛潜行特别版', 'Wikelo Speedy Special': '维克洛疾速特别版',
    'Wikelo Savior Special': '维克洛救援特别版', 'Wikelo Special': '维克洛特别版', "Teach's Special": '蒂奇特别版',
    'PYAM Exec': 'PYAM 行政版', 'Utility': '通用型', 'Assault': '突击型', 'Rambler': '漫游者', 'Scout': '侦察型',
}
# Special editions are labels, shown in （） like the other variants.
SPECIAL = ['Wikelo War Special', 'Wikelo Work Special', 'Wikelo Sneak Special', 'Wikelo Speedy Special',
           'Wikelo Savior Special', 'Wikelo Special', "Teach's Special", 'Executive Edition', 'PYAM Exec']
# Groups whose class names have no localization key of their own.
GROUP = {
    'ANVL_Carrack': '铁砧卡拉克', 'ANVL_C8R_Pisces': '铁砧 C8R 双鱼座', 'ANVL_Paladin': '铁砧圣骑士', 'ANVL_Hornet': '铁砧大黄蜂',
    'ARGO_MOLE': '阿尔戈MOLE', 'Argo_MPUV': '阿尔戈MPUV', 'BANU_Defender': '巴努捍卫者', 'CRUS_Spirit': '十字军精灵',
    'GAMA_Railen': '伽塔克锐伦', 'GRIN_UTV': '灰猫UTV', 'KRIG_L22': '克鲁格 L-22 阿尔法狼', 'MISC_Fury': 'MISC 狂怒',
    'MISC_Starlancer': 'MISC 星际枪骑兵', 'Orbital_Sentry': '轨道哨兵', 'ORIG_85x': '奥源85X', 'ORIG_m80': '奥源 M80',
    'probe_comms': '通讯探测器', 'RSI_Aurora': 'RSI极光', 'RSI_Ursa': 'RSI熊', 'Spaceship_Template': '飞船模板',
    'XNAA_SanTokYai': '奥珀圣托克亚', 'EAObjectiveDestructable_MiningLaser': '任务目标·采矿激光',
    'EAObjectiveDestructable_Satellite': '任务目标·卫星',
}
# Variant suffix tokens; anything missing stays as it is.
TOKEN = {
    'AI': 'AI', 'CIV': '民用', 'Civ': '民用', 'Civilian': '民用', 'CRIM': '罪犯', 'Crim': '罪犯', 'GenCrim': '普通罪犯',
    'PIR': '海盗', 'Pirate': '海盗', 'BH': '赏金猎人', 'SEC': '安保', 'UEE': 'UEE', 'NT': '九尾', 'FF': '边境战士',
    'Advocacy': '执法局', 'Xenothreat': '异种威胁', 'BoardedCombat': '登舰战', 'NoneBoarded': '无登舰', 'Boarded': '登舰',
    'ShipBoarded': '登舰', 'NoInterior': '无内饰', 'Hijacked': '被劫持', 'Derelict': '残骸', 'Wreck': '残骸',
    'NoDebris': '无碎片', 'Teach': '蒂奇', 'Collector': '收藏', 'Military': '军用', 'Indust': '工业', 'Stealth': '潜行',
    'Medic': '医疗', 'Competition': '竞速', 'Showdown': '对决', 'ShipShowdown': '对决', 'FleetWeek': '舰队周',
    'Template': '模板', 'Unmanned': '无人', 'Salvage': '打捞', 'LowFuel': '低燃料', 'Super': '超级', 'GameMaster': 'GM',
    'Invictus': '无畏周', 'CitizenCon': '公民大会', 'Indestructible': '不可摧毁', 'Drug': '毒品', 'Temp': '临时',
    'Expedition': '远征', 'DEF': '防御', 'Crusader': '十字军', 'Microtech': '微科', 'Hurston': '赫斯顿', 'ArcCorp': '弧光',
    'VAN': '剜度', 'Exec': '行政', 'DarkBlue': '深蓝', 'Fleetweek': '舰队周', 'NineTails': '九尾', 'Ninetails': '九尾',
    'BIS2950': '最佳舰船展2950', 'BIS2024': '最佳舰船展2024', 'TEST': '测试', 'Test': '测试', 'TEMP': '临时',
    'Elite': '精英', 'Supreme': '至尊', 'Stunt': '特技', 'Spawn': '生成', 'Tier': '等级', 'AlphaWolf': '阿尔法狼',
    'DefendShip': '护卫目标', 'CFP': '繁荣公民', 'BlacJac': '黑杰克', 'Override': '覆盖', 'Max': 'MAX', 'Gemini': '双子座', 'Body': '舰体', 'Nose': '舰首',
    'Tail': '舰尾', 'Left': '左', 'Right': '右', 'Lootable': '可搜刮', 'Rescue': '救援', 'Emerald': '翡翠',
}
# Place names without an official translation, matched case-insensitively before single words.
PLACE_PROPER = {
    'Levski': '列夫斯基', 'Ruin Station': '废墟空间站', 'Checkmate': '将军站', 'Orbituary': '轨道墓园',
    'Glaciem Ring': '冰川环', 'Keeger Belt': '基格尔带', 'Aaron Halo': '亚伦光环', 'New Babbage': '新巴贝奇',
    'Jump point to': '跳跃点至',
}
# Words in the game's internal place names (scan results, interiors, belt segments).
PLACE_WORDS = {
    'segment': '分段', 'int': '内部', 'interior': '内部', 'intoc': '内部容器', 'ab': '小行星带', 'mine': '矿点',
    'keeger': '基格尔带', 'glaciemring': '冰川环', 'med': '中', 'mid': '中', 'middle': '中部', 'base': '基地',
    'objectcontainer': '对象容器', 'objectcontainermodifier': '对象容器修饰', 'locationobjectcontainer': '地点对象容器',
    'locationharvestableobjectcontainer': '可采集对象容器', 'oc': '对象容器', 'entrance': '入口', 'entry': '入口',
    'lrg': '大', 'lge': '大', 'sml': '小', 'sm': '小', 'sfce': '表面', 'surface': '地表', 'surfaceentrance': '地表入口',
    'mission': '任务', 'genrl': '通用', 'lobby': '大厅', 'rckcrk': '岩缝', 'rs': '休息站', 'rstop': '休息站',
    'reststop': '休息站', 'outlaw': '法外者', 'tsg': 'TSG', 'gascloud': '气体云', 'elev': '电梯', 'reception': '接待处',
    'orison': '奥瑞森', 'lorville': '罗威尔', 'levski': '列夫斯基', 'nose': '舰首', 'tail': '舰尾', 'rear': '后部',
    'social': '社交区', 'delta': '德尔塔', 'comm': '通讯', 'habs': '居住舱', 'hab': '居住舱', 'restaurant': '餐厅',
    'office': '办公室', 'occu': '占用', 'to': '至', 'single': '单人', 'point': '点', 'jump': '跳跃', 'side': '侧',
    'rund': '破旧', 'rundown': '破旧', 'ext': '外部', 'newbab': '新巴贝奇', 'mic': '微科', 'hur': '赫斯顿',
    'cru': '十字军', 'arc': '弧科', 'stan': '斯坦顿', 'layout': '布局', 'gate': '闸门', 'dung': '地牢',
    'opendungeon': '开放地牢', 'temp': '临时', 'hull': '舰体', 'hatch': '舱口', 'hangar': '机库', 'gym': '健身房',
    'exec': '行政', 'enctr': '遭遇', 'ctplr': '中庭', 'ctpl': '中庭', 'wing': '机翼', 'util': '公用', 'transit': '交通',
    'rewards': '奖励', 'orbital': '轨道', 'orbtl': '轨道', 'npc': 'NPC', 'lrgfrnt': '大前部', 'medfrnt': '中前部',
    'drlct': '残骸', 'cz': '争夺区', 'contestedzone': '争夺区', 'cargo': '货运', 'back': '后', 'spaceport': '航天港',
    'refinery': '精炼厂', 'refin': '精炼', 'refindeck': '精炼甲板', 'topdeck': '上甲板', 'middeck': '中甲板',
    'bottomdeck': '下甲板', 'teachsshipshop': '蒂奇飞船商店', 'ruinstation': '废墟空间站', 'rgt': '右', 'lft': '左',
    'plat': '平台', 'ovgr': '杂草丛生', 'master': '主', 'main': '主', 'hospital': '医院', 'dealership': '经销店',
    'command': '指挥', 'center': '中心', 'cbd': '中央商务区', 'asteroidbase': '小行星基地', 'arcade': '游戏厅',
    'tower': '塔', 'domes': '穹顶', 'commercial': '商业区', 'ground': '地面', 'final': '终版', 'sp': '出生点',
    'a18': '18区', 'stanton': '斯坦顿', 'pyro': '派罗', 'nyx': '尼克斯', 'terra': '特拉', 'magnus': '马格努斯',
    'castra': '卡斯特拉', 'jp': '跳跃点', 'dummy': '占位', 'incredifun': '无敌乐园', 'iae': '星际航空展',
    'starun': '星际联合', 'wtn': 'WTN', 'mose': 'MOSE', 'bsd': 'BSD', 'v2': 'V2', 'leo': '低轨道',
}
ROMAN = {'1': 'I', '2': 'II', '3': 'III', '4': 'IV', '5': 'V', '6': 'VI'}

def place_token(t, bodies):
    low = t.lower()
    if low in PLACE_WORDS: return PLACE_WORDS[low]
    m = re.fullmatch(r'region([a-z])', low)
    if m: return '区域' + m.group(1).upper()
    m = re.fullmatch(r'(stanton|pyro|nyx)(\d)', low)
    if m: return bodies.get(m.group(1).capitalize() + m.group(2), t)
    m = re.fullmatch(r'p(\d)(l\d|leo)', low)             # p5l2 = Pyro V L2
    if m: return f"派罗{ROMAN.get(m.group(1), m.group(1))} {m.group(2).upper() if m.group(2) != 'leo' else '低轨道'}"
    m = re.fullmatch(r'jp(\d+)', low)
    if m: return '跳跃点' + m.group(1)
    m = re.fullmatch(r'oc(\d+)', low)
    if m: return '对象容器' + m.group(1)
    m = re.fullmatch(r'gate(\d+)', low)
    if m: return '闸门' + m.group(1)
    if re.fullmatch(r'\d+[a-z]?|[a-z]{1,3}|l\d+|[ivx]+', low): return t.upper() if not t[0].isdigit() else t
    return t

def translate_place(shown, proper, bodies):
    text = shown
    for en_name in sorted(proper, key=len, reverse=True):
        text = re.sub(r'(?<![A-Za-z0-9])' + re.escape(en_name) + r'(?![A-Za-z0-9])', '\0' + proper[en_name] + '\0', text,
                      flags=re.I)
    out = []
    for i, chunk in enumerate(text.split('\0')):
        if i % 2: out.append(chunk); continue
        out.extend(place_token(t, bodies) for t in re.split(r'[\s_\-]+', chunk) if t)
    return tidy(join(*out))

SYSTEMS = {'stanton': '斯坦顿', 'pyro': '派罗', 'nyx': '尼克斯', 'terra': '特拉', 'magnus': '马格努斯', 'castra': '卡斯特拉'}

def read_ini(path):
    d = {}
    with open(path, encoding='utf-8-sig', errors='replace') as f:
        for line in f:
            if '=' not in line: continue
            k, v = line.rstrip('\r\n').split('=', 1)
            v = v.replace('\\n', '').strip()
            if v: d[k.split(',')[0]] = v
    return d

def join(*parts):
    """Chinese pieces touch; a space only goes between two ASCII-ending/-starting pieces."""
    out = ''
    for p in parts:
        if not p: continue
        if out and out[-1].isascii() and out[-1].isalnum() and p[0].isascii() and p[0].isalnum(): out += ' '
        out += p
    return out

CJK = re.compile(r'(?<=[\u3000-\u9fff\uff00-\uffef]) +| +(?=[\u3000-\u9fff\uff00-\uffef])')

def tidy(name):
    return CJK.sub('', name)

def translate_phrase(text, table):
    words, out = text.split(), []
    i = 0
    while i < len(words):
        for n in range(len(words) - i, 0, -1):
            phrase = ' '.join(words[i:i + n])
            if phrase in table:
                out.append(table[phrase]); i += n; break
        else:
            out.append(words[i]); i += 1
    return join(*out)

def strip_maker(en_name):
    for maker in sorted(MAKER_EN, key=len, reverse=True):
        if en_name.startswith(maker + ' '): return MAKER_EN[maker], en_name[len(maker) + 1:]
    return '', en_name

def base_name(key, zh, en):
    """Chinese name and special-edition label for a localization key (without 'vehicle_Name')."""
    if 'vehicle_Name' + key in zh: return zh['vehicle_Name' + key], ''
    en_name, special = en['vehicle_Name' + key], ''
    for sp in SPECIAL:
        if en_name.endswith(' ' + sp):
            en_name, special = en_name[:-len(sp) - 1], WORDS[sp]
            break
    return plain_name(key, en_name, zh, en), special

def plain_name(key, en_name, zh, en):
    toks = key.split('_')
    for n in range(len(toks) - 1, 0, -1):
        parent = '_'.join(toks[:n])
        pz, pe = zh.get('vehicle_Name' + parent), en.get('vehicle_Name' + parent)
        if pz and pe and en_name.startswith(pe + ' '):
            return join(pz, translate_phrase(en_name[len(pe) + 1:], WORDS)) if en_name != pe else pz
        if pz and pe and en_name == pe:
            return pz
    maker, model = strip_maker(en_name)
    return join(maker or MAKER_PREFIX.get(toks[0], ''), translate_phrase(model, {**MODEL, **WORDS}))

def label(tokens, keep_pu):
    out = []
    for t in tokens:
        if t == 'PU' and not keep_pu: continue
        if re.fullmatch(r'[A-Za-z]$', t): out.append(t.upper()); continue
        out.append(TOKEN.get(t, t))
    return '·'.join(out)

def ship_name(cls, zh, en, keep_pu=False):
    toks = cls.split('_')
    for n in range(len(toks), 0, -1):
        key = '_'.join(toks[:n])
        if 'vehicle_Name' + key in zh or 'vehicle_Name' + key in en:
            (base, special), rest = base_name(key, zh, en), toks[n:]
            break
    else:
        for prefix in sorted(GROUP, key=len, reverse=True):
            if cls.lower().startswith(prefix.lower()) and (len(cls) == len(prefix) or cls[len(prefix)] == '_'):
                base, special, rest = GROUP[prefix], '', [t for t in cls[len(prefix):].split('_') if t]
                break
        else:
            return None
    tag = '·'.join(t for t in (special, label(rest, keep_pu)) if t)
    return tidy(f'{base}（{tag}）' if tag else base)

def main(loc_dir, data_dir):
    zh = read_ini(os.path.join(loc_dir, 'chinese_(simplified)', 'global.ini'))
    en = read_ini(os.path.join(loc_dir, 'english', 'global.ini'))
    ships = [l.split('#')[0].strip() for l in open(os.path.join(data_dir, 'ships.txt'), encoding='utf-8')]
    ships = [s for s in ships if s]
    names = {c: ship_name(c, zh, en) for c in ships}
    seen = {}
    for c, n in names.items(): seen.setdefault(n, []).append(c)
    for n, cs in seen.items():          # dropping PU made two classes look alike: keep it for those
        if n and len(cs) > 1:
            for c in cs: names[c] = ship_name(c, zh, en, keep_pu=True)
    # Still alike (NT and NineTails both read 九尾, or a variant sharing its base's localized name):
    # add what tells the classes apart, translated if that is enough, raw otherwise.
    seen = {}
    for c, n in names.items(): seen.setdefault(n, []).append(c)
    for n, cs in seen.items():
        if not n or len(cs) < 2: continue
        toks = [c.split('_') for c in cs]
        common = 0
        while all(len(t) > common and t[common] == toks[0][common] for t in toks): common += 1
        tags = {c: label(t[common:], True) for c, t in zip(cs, toks)}
        if len(set(tags.values())) < len(cs): tags = {c: '·'.join(t[common:]) for c, t in zip(cs, toks)}
        for c in cs:
            if tags[c]: names[c] = tidy(n[:-1] + '·' + tags[c] + '）' if n.endswith('）') else f'{n}（{tags[c]}）')
    missing = [c for c, n in names.items() if not n]
    with open(os.path.join(data_dir, 'ship_names_zh.txt'), 'w', encoding='utf-8') as f:
        f.write('# 飞船中文名：代号|中文名。由 tools/zh-cn/gen-sc-offline-zh-names.py 从游戏语言包生成。\n')
        for c in ships:
            if names[c]: f.write(f'{c}|{names[c]}\n')
    print(f'ships: {len(ships) - len(missing)} named, {len(missing)} left as class names')

    places = {f'system:{k}': f'{v}星系' for k, v in SYSTEMS.items()}
    bodies = {}
    for k, v in zh.items():             # Stanton1 赫斯顿星, Stanton1a 阿里尔, Stanton1_L1 HUR L1, Pyro4 派罗IV
        if re.fullmatch(r'(Stanton|Pyro|Nyx|Terra|Castra|Magnus)\d+[a-z]?(_L\d)?', k): places[k] = bodies[k] = v
    # English proper names with an official Chinese name (Orison 奥瑞森, Monox 派罗II, Area18 18区 ...).
    proper = dict(PLACE_PROPER)
    for k, v in zh.items():
        e = en.get(k)
        if e and len(e.split()) <= 3 and re.fullmatch(r'(Stanton|Pyro|Nyx|RR_|AaronHalo|Nyx_AsteroidBelt)\w*', k) \
                and not re.search(r'desc|_short|Desc', k) and re.fullmatch(r"[A-Za-z0-9' .-]+", e):
            proper.setdefault(e, v)
    for fname in ('locations.txt', 'locations_found.txt'):
        path = os.path.join(data_dir, fname)
        if not os.path.exists(path): continue
        for line in open(path, encoding='utf-8', errors='replace'):
            parts = [p.strip() for p in line.split('|')]
            if len(parts) < 3 or line.startswith('#'): continue
            m = re.fullmatch(r'(?:OOC_)?jump_?point_(\w+?)_(\w+)', parts[2], re.I)
            if m:
                a, b = m.group(1).lower(), m.group(2).lower()
                places[parts[2]] = f'跳跃点（{SYSTEMS.get(a, a)} → {SYSTEMS.get(b, b)}）'
                continue
            # OOC_<body> entities resolve through the body keys above, like the mod does.
            mo = re.match(r'OOC_([^_]+)_([^_]+)', parts[2], re.I)
            if mo and (mo.group(1) + mo.group(2) in places or mo.group(1) + '_' + mo.group(2) in places): continue
            zh_name = translate_place(parts[1], proper, bodies)
            if zh_name and zh_name != parts[1]: places[parts[2]] = zh_name
    with open(os.path.join(data_dir, 'place_names_zh.txt'), 'w', encoding='utf-8') as f:
        f.write('# 地点中文名：键|中文名。键是实体名、语言包天体键（Stanton1a）或 system:<星系>。\n')
        for k in sorted(places): f.write(f'{k}|{places[k]}\n')
    print(f'places: {len(places)} names')

main(sys.argv[1], sys.argv[2])
