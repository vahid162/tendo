import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

from verify_simulation import verify_images, verify_serial


class TestEvidence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        for side in ("left", "right"):
            for pose, y in (("idle", 100), ("pet", 70)):
                frame = Image.new("RGBA", (240, 240), (0, 0, 0, 255))
                ImageDraw.Draw(frame).ellipse((70, y, 170, y + 70), fill=(65, 235, 255, 255))
                frame.save(self.folder / f"{side}-{pose}.png")

    def test_both_nonblank_and_changed(self):
        result = verify_images(self.folder)
        self.assertEqual(set(result), {"left", "right"})
        self.assertGreater(result["left"]["changed_fraction"], 0.01)

    def test_bad_frame_on_either_eye_is_rejected(self):
        for color in ((0, 0, 0, 255), (255, 255, 255, 255), (65, 235, 255, 0)):
            for side in ("left", "right"):
                with self.subTest(side=side, color=color):
                    target = self.folder / f"{side}-pet.png"
                    original = target.read_bytes()
                    Image.new("RGBA", (240, 240), color).save(target)
                    with self.assertRaises(ValueError):
                        verify_images(self.folder)
                    target.write_bytes(original)

    def test_missing_corrupt_or_wrong_size(self):
        frame = self.folder / "right-pet.png"
        frame.unlink()
        with self.assertRaises(ValueError):
            verify_images(self.folder)
        frame.write_bytes(b"this is not a PNG")
        with self.assertRaises(ValueError):
            verify_images(self.folder)
        Image.new("RGB", (20, 20)).save(frame)
        with self.assertRaises(ValueError):
            verify_images(self.folder)

    def test_unchanged_pet_is_rejected(self):
        (self.folder / "right-pet.png").write_bytes((self.folder / "right-idle.png").read_bytes())
        with self.assertRaisesRegex(ValueError, "right.*unchanged"):
            verify_images(self.folder)

    def test_serial_requires_real_boot_and_events(self):
        serial = "\n".join([
            "EYES READY", "REACTION LED READY", "TANDO FIRMWARE v0.10.0-rc.8",
            "TANDO FINAL 30-MIN DEMO READY", "PET REACTION - CLEAR AFFECTIONATE UP LOOK",
            "FOOD TAG 1 ACCEPTED", "FOOD REACTION",
            "SLEEP STATE ENTERED - WAITING FOR TAG REMOVAL", "SLEEP TAG REMOVED -> WAKE UP",
        ])
        self.assertEqual(verify_serial(serial, "0.10.0-rc.8"), [])
        for absent in ("EYES READY", "FOOD REACTION", "SLEEP TAG REMOVED -> WAKE UP"):
            with self.subTest(absent=absent), self.assertRaises(ValueError):
                verify_serial(serial.replace(absent, ""), "0.10.0-rc.8")
        for crash in ("Guru Meditation Error", "Backtrace: 0x400", "FATAL: CANVAS ALLOCATION FAILED"):
            with self.subTest(crash=crash), self.assertRaises(ValueError):
                verify_serial(serial + "\n" + crash, "0.10.0-rc.8")
        with self.assertRaises(ValueError):
            verify_serial(serial, "0.10.0-rc.9")
        self.assertTrue(verify_serial(serial + "\nspiAttachMISO(): HSPI Does not have default pins on ESP32S3!", "0.10.0-rc.8"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
