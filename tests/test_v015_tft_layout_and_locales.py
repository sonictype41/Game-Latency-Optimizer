from pathlib import Path
from html.parser import HTMLParser
ROOT=Path(__file__).resolve().parents[1]
class Games(HTMLParser):
 def __init__(self): super().__init__();self.games=[]
 def handle_starttag(self,tag,attrs):
  a=dict(attrs)
  if tag=='div' and a.get('data-game'):self.games.append(a['data-game'])
def test_four_games_artwork_and_native_official_handoff():
 html=(ROOT/'app/frontend/ui/index.html').read_text(encoding='utf8')
 js=(ROOT/'app/frontend/ui/app.js').read_text(encoding='utf8')
 cpp=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf8')
 p=Games();p.feed(html)
 assert p.games==['valorant','roblox','pubg','tft']
 assert (ROOT/'app/frontend/ui/assets/tft-presskit.jpg').stat().st_size>50000
 assert '*game=="tft"' in cpp
 assert "tft:'tft-presskit.jpg'" in js
 assert "'art.label':'Định tuyến nâng cao'" in js
 assert "'art.label':'Advanced Routing'" in js
 assert js.count("'art.label':'Advanced Routing'")==1
 assert 'id="stat-phase"' not in html
 assert "updateText('stat-phase'" not in js
 assert 'art-session-pill' in html
def test_window_four_card_clamp_and_dpi():
 cpp=(ROOT/'app/frontend/main.cpp').read_text(encoding='utf8')
 css=(ROOT/'app/frontend/ui/style.css').read_text(encoding='utf8')
 assert 'kClientWidth = 960' in cpp
 assert 'ptMaxTrackSize.x=info->ptMinTrackSize.x' in cpp
 assert 'MulDiv(kClientWidth,GetDpiForSystem(),96)' in cpp
 assert 'grid-template-columns:repeat(4,minmax(0,210px))' in css
 assert 4*210+3*17+2*32<=960
