"""Regression: PUBG must be a direct child of the Services game gallery."""
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class GalleryParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.div_stack = []
        self.cards = []
        self.gallery_count = 0

    def handle_starttag(self, tag, attrs):
        if tag != 'div':
            return
        info = dict(attrs)
        name = info.get('id', '')
        if name == 'game-col':
            self.gallery_count += 1
        if 'game-card' in info.get('class', '').split():
            assert self.div_stack and self.div_stack[-1] == 'game-col', (
                f"Game card {info.get('data-game')} is outside the game gallery"
            )
            self.cards.append(info.get('data-game'))
        self.div_stack.append(name)

    def handle_endtag(self, tag):
        if tag == 'div':
            assert self.div_stack, 'Unbalanced div in UI markup'
            self.div_stack.pop()

def test_published_pubg_card_in_services_gallery():
    parser = GalleryParser()
    parser.feed((ROOT/'app/frontend/ui/index.html').read_text(encoding='utf-8'))
    assert parser.gallery_count == 1
    assert parser.cards == ['valorant', 'roblox', 'pubg', 'tft']
    assert not parser.div_stack
    assert (ROOT/'app/frontend/ui/assets/pubg-presskit.jpg').is_file()


def test_pubg_catalog_and_connected_artwork_mapping():
    script = (ROOT/'app/frontend/ui/app.js').read_text(encoding='utf-8')
    assert "catalog.length>0&&!found" in script
    assert "g.id===card.dataset.game||g.game===card.dataset.game" in script
    assert "pubg:'pubg-presskit.jpg'" in script
    assert "send('services.connect',{game,region:state.region,routing_preference:state.artEnabled?'advanced':'standard'})" in script
