# -*- coding: utf-8 -*-
"""M20-F2 天气/温湿数据契约：调度、温度、视觉/尸速缩放与衣物接口。"""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "game.json"
ITEMS = ROOT / "assets" / "items.json"


class WeatherDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.weather = root["weather"]
        cls.climate = root["climate"]
        cls.items = json.loads(ITEMS.read_text(encoding="utf-8"))["items"]
        cls.by_id = {item["id"]: item for item in cls.items}

    def test_weather_schedule_is_tunable(self):
        cfg = self.weather
        self.assertTrue(cfg["enabled"])
        for key in ("clearWeight", "rainWeight", "fogWeight", "coldSnapWeight"):
            self.assertGreaterEqual(cfg[key], 0.0, key)
        self.assertGreater(sum(cfg[key] for key in
                               ("clearWeight", "rainWeight", "fogWeight",
                                "coldSnapWeight")), 0.0)
        self.assertGreater(cfg["periodMinutes"], 0.0)
        self.assertGreater(cfg["transitionMinutes"], 0.0)
        self.assertLess(cfg["transitionMinutes"], cfg["periodMinutes"])
        self.assertLess(cfg["coldSnapTemperatureC"], cfg["rainTemperatureC"])
        self.assertLess(cfg["rainTemperatureC"], cfg["fogTemperatureC"])
        self.assertLess(cfg["fogTemperatureC"], cfg["clearTemperatureC"])
        self.assertGreater(cfg["nightTemperatureDropC"], 0.0)

    def test_weather_effects_are_lightweight_and_bounded(self):
        cfg = self.weather
        for key in ("rainSightScale", "fogSightScale", "coldSnapSightScale",
                    "rainMoveScale", "fogMoveScale", "coldSnapMoveScale"):
            self.assertGreater(cfg[key], 0.0, key)
            self.assertLessEqual(cfg[key], 1.0, key)
        self.assertLess(cfg["fogSightScale"], cfg["rainSightScale"])
        self.assertGreaterEqual(cfg["coldSnapMoveScale"], 0.85)

    def test_climate_thresholds_and_clothing_fields(self):
        cfg = self.climate
        self.assertLess(cfg["coldStartC"], cfg["comfortC"])
        self.assertGreater(cfg["coldWarnC"], cfg["hypothermiaThresholdC"])
        self.assertGreater(cfg["comfortC"], cfg["coldWarnC"])
        self.assertGreater(cfg["indoorWarmthC"], 0.0)
        self.assertGreater(cfg["fireWarmthC"], cfg["indoorWarmthC"])
        self.assertGreater(cfg["coldFatiguePerSecond"], 0.0)
        self.assertGreater(cfg["hypothermiaDamagePerSecond"], 0.0)
        self.assertGreater(cfg["campfireRadius"], 0.0)

        raincoat = self.by_id["raincoat"]["armor"]
        jacket = self.by_id["jacket"]["armor"]
        padded = self.by_id["padded_jacket"]["armor"]
        self.assertGreater(raincoat["waterproof"], jacket["waterproof"])
        self.assertGreater(padded["insulation"], raincoat["insulation"])


if __name__ == "__main__":
    unittest.main()
