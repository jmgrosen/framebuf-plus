import unittest
import epd
import framebuf_plus
import time
from array import array
try:
    from FiraSansBold16pt import FiraSansBold16pt as GFXFont
    is_test_font = True
except:
    is_test_font = False

class TestFrameBuffer(unittest.TestCase):
    def __init__(self):
        self.e = epd.EPD47()
        self.buffer = bytearray(int(960 * 540 / 2))
        self.fb = framebuf_plus.FrameBuffer(self.buffer, 960, 540, framebuf_plus.GS4_HLSB)
        self.fb.fill(15)

    def setUpClass(self):
        self.e.power(True)
        self.e.clear()

    def tearDownClass(self):
        self.e.power(False)
        del self.e
        del self.fb
        del self.buffer

    def setUp(self):
        self.e.clear()

    def tearDown(self):
        self.e.bitmap(self.buffer, 0, 0, 960, 540)
        time.sleep(1)

    def test_hline(self):
        self.fb.hline(0, 50, 100, 0)

    def test_vhine(self):
        self.fb.vline(150, 0, 100, 0)

    def test_line(self):
        self.fb.line(200, 0, 300, 100, 0)
        self.fb.line(300, 0, 200, 100, 0)
    
    def test_rect(self):
        self.fb.rect(300, 0, 100, 100, 0)

    def test_fill_rect(self):
        self.fb.rect(400, 0, 100, 100, 0, True)

    def test_circle(self):
        self.fb.ellipse(550, 50, 50, 50, 0)

    def test_fill_circle(self):
        self.fb.ellipse(650, 50, 50, 50, 0, True)

    def test_triangle(self):
        self.fb.poly(700, 0, array('h', [0, 0, 0, 100, 100, 100]), 0)

    def test_fill_triangle(self):
        self.fb.poly(800, 0, array('h', [0, 0, 0, 100, 100, 100]), 0, True)

    @unittest.skipUnless(is_test_font, "No gfx font file, skip")
    def test_text_text(self):
        try:
            self.fb.gfx(GFXFont)
            self.fb.write("1234567890", 0, 200, 0)
            self.fb.write("abcdefghijklmnopqrstuvwyz", 0, 300, 0)
            self.fb.write("ABCDEFGHIJKLMNOPQRSTUVWYZ", 0, 400, 0)
            self.fb.gfx()
        except:
            pass

if __name__ == "__main__":
    unittest.main()
