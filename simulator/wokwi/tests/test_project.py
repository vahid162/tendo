import json
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class TestTandoSimulator(unittest.TestCase):
    def test_two_eyes(self):
        doc=json.loads((ROOT/'diagram.json').read_text(encoding='utf-8'))
        parts=[p for p in doc['parts'] if p['type']=='chip-gc9a01']
        self.assertEqual(len(parts),2)
        self.assertEqual(len({p['id'] for p in doc['parts']}), len(doc['parts']))
        self.assertEqual({p['id'] for p in parts},{'eyeLeft','eyeRight'})
        board=next(p for p in doc['parts'] if p['id']=='esp')
        self.assertEqual(board['type'],'board-esp32-s3-devkitc-1')
        self.assertEqual(board['attrs']['flashSize'],'8')
        self.assertEqual(board['attrs']['psramSize'],'2')
        edges={frozenset(x[:2]) for x in doc['connections']}
        for side, control in [('eyeLeft',{'DC':6,'CS':7,'RST':15}),('eyeRight',{'DC':18,'CS':16,'RST':17})]:
            for pin,num in control.items():
                self.assertIn(frozenset((f'esp:{num}',f'{side}:{pin}')),edges)
            for pin,num in [('SCL',4),('SDA',5)]:
                self.assertIn(frozenset((f'esp:{num}',f'{side}:{pin}')),edges)
            self.assertIn(frozenset(('esp:3V3.1',f'{side}:VCC')),edges)
            self.assertIn(frozenset(('esp:GND.1',f'{side}:GND')),edges)
    def test_chip(self):
        p=json.loads((ROOT/'gc9a01.chip.json').read_text(encoding='utf-8'))
        self.assertEqual(p['display'],{'width':240,'height':240})
        cfg=(ROOT/'wokwi.toml').read_text(encoding='utf-8')
        self.assertIn("name = 'gc9a01'",cfg)
        self.assertIn("binary = 'gc9a01.chip.wasm'",cfg)
    def test_firmware_adapter(self):
        s=(ROOT/'sketch.ino').read_text(encoding='utf-8')
        self.assertIn('0.10.0-rc.8',s)
        self.assertIn('Serial.begin(115200)',s)
        self.assertNotIn('Serial.init(115200)',s)
        self.assertIn('#define TANDO_WOKWI_MODE 1',s)
        self.assertIn('handlePetEvent(now)',s)
        self.assertIn('handleFoodEvent(1, now)',s)
        self.assertIn('#if !TANDO_WOKWI_MODE',s)
        self.assertIn('#include "../sketch.ino"',(ROOT/'src/main.cpp').read_text())

if __name__=='__main__':
    unittest.main(verbosity=2)
