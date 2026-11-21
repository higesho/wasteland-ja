# 翻訳に回す原文を分ける: work/chunks/NN.json（[{k: 番号, map: 場所, en: 記号つき原文}]）
#   原文が同じものは 1 回だけ（最初に出た所）。合言葉（小文字のない短い語）・数字・壊れたデータは外す
import sys, os, json, re, collections
sys.path.insert(0, os.path.dirname(__file__))
from tok import to_tok
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
d = json.load(open(os.path.join(ROOT, 'work', 'strings.json'), encoding='utf-8'))

COMMON = set('the a an you your is are of to and in it that on with for at as be this by from he she they was there his her them not but have has can will all one out up into'.split())

def is_junk(t):
    letters = sum(c.isalpha() for c in t)
    if letters == 0: return True
    words = re.findall(r"[A-Za-z']+", t)
    if not words: return True
    if '\r' in t or '\x06' in t or '\x07' in t or '\x04' in t: return False
    # 区切りのない英字の塊・母音のない語が多いものは壊れたデータ
    bad = sum(1 for w in words if len(w) > 2 and not re.search(r'[aeiouyAEIOUY]', w))
    if bad >= 2: return True
    if re.search(r'[a-z]{2,}[A-Z]', t) and len(words) < 4: return True
    if re.search(r' {3,}', t) and not t.strip().istitle(): return True
    if re.search(r',,|\x7f', t): return True
    return False

def is_keyword(t):
    s = t.strip()
    return s and not re.search(r'[a-z]', s) and '\r' not in t and len(s) <= 24 and not re.search(r'[\x00-\x1f]', s)

names = {}
for x in d:
    m = re.match(r'(g\d:\d+):', x['id'])
    if m and m.group(1) not in names:
        mm = re.search(r'Entering ([^.\r!]+)', x['text'])
        if mm: names[m.group(1)] = mm.group(1)

seen = {}
items = []
skipped = collections.Counter()
keywords = collections.defaultdict(list)
for x in d:
    t = x['text']; i = x['id']
    if not t.strip(): continue
    if not i.startswith('g'): continue           # EXE・怪物の名前などは別に扱う
    if is_keyword(t):
        keywords[i.rsplit(':', 1)[0]].append(t.strip()); skipped['kw'] += 1; continue
    if re.fullmatch(r'[\d\s\-.]+', t): skipped['num'] += 1; continue
    if is_junk(t): skipped['junk'] += 1; continue
    if t in seen: continue
    seen[t] = i
    mp = i.rsplit(':', 1)[0]
    items.append({'k': len(items), 'id': i, 'map': names.get(mp, mp), 'kw': mp, 'en': to_tok(t)})

json.dump(dict(keywords), open(os.path.join(ROOT, 'work', 'keywords.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
os.makedirs(os.path.join(ROOT, 'work', 'chunks'), exist_ok=True)
# 3500 字ほどで切る（同じマップはなるべく同じ塊に）
chunks = []; cur = []; size = 0
for it in items:
    if cur and (size + len(it['en']) > 3500):
        chunks.append(cur); cur = []; size = 0
    cur.append(it); size += len(it['en'])
if cur: chunks.append(cur)
for n, c in enumerate(chunks):
    kws = sorted(set(k for it in c for k in keywords.get(it['kw'], [])))
    json.dump({'keywords': kws, 'items': [{'k': it['k'], 'map': it['map'], 'en': it['en']} for it in c]},
              open(os.path.join(ROOT, 'work', 'chunks', '%02d.json' % n), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
json.dump(items, open(os.path.join(ROOT, 'work', 'items.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
print('items', len(items), 'chars', sum(len(i['en']) for i in items), 'chunks', len(chunks), dict(skipped))
print('maps', names)
