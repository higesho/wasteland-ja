# 訳をまとめて text/ja.json（{原文: 訳}、制御の字は生のまま）を作る
#   work/out/NN.json（本文の訳）+ text/ui.json（EXE・UI）+ text/names.json（怪物の名前）
#   先頭・末尾の改行は英語に合わせる。合言葉が消えていたら知らせる
import sys, os, json, re, glob
sys.path.insert(0, os.path.dirname(__file__))
from tok import from_tok
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

def fix_nl(en, ja):
    le = len(en) - len(en.lstrip('\n')); te = len(en) - len(en.rstrip('\n'))
    core = ja.strip('\n')
    return '\n' * le + core + '\n' * te

def main():
    out = {}
    warn = 0
    for f in sorted(glob.glob(os.path.join(ROOT, 'work', 'chunks', '[0-9]*.json'))):
        n = os.path.basename(f)
        c = json.load(open(f, encoding='utf-8'))
        r = json.load(open(os.path.join(ROOT, 'work', 'out', n), encoding='utf-8'))
        for it in c['items']:
            en = it['en']; ja = r[str(it['k'])]
            for kw in c['keywords']:
                if len(kw) >= 3 and re.search(r'\b%s\b' % re.escape(kw), en) and kw not in ja:
                    print('keyword lost', n, it['k'], kw, '|', ja[:80]); warn += 1
            out[from_tok(en)] = from_tok(fix_nl(en, ja))
    ids = {}
    for l in open(os.path.join(ROOT, 'work', 'ui_en.txt'), encoding='utf-8'):
        i, t = l.rstrip('\n').split('\t', 1); ids[i] = json.loads(t)
    for k, ja in json.load(open(os.path.join(ROOT, 'text', 'ui.json'), encoding='utf-8')).items():
        out[from_tok(ids[k])] = from_tok(ja)
    for en, ja in json.load(open(os.path.join(ROOT, 'text', 'names.json'), encoding='utf-8')).items():
        out[from_tok(en)] = from_tok(ja)
    for k in list(out):
        if out[k] == k: del out[k]
    json.dump(out, open(os.path.join(ROOT, 'text', 'ja.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    print('entries', len(out), 'warnings', warn)

if __name__ == '__main__':
    main()
