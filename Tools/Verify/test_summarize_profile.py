"""Tests de summarize_profile.py.

Uso: uv run --with numpy --with pillow python -m unittest discover -s Tools/Verify -p "test_*.py"
"""

import unittest

from summarize_profile import classify_path, exclusive_times, summarize_case, view_of

MAIN = "Frame/SceneRender - ViewFamilies/RenderGraphExecute - |ViewFamilies/Scene"
CAPTURE = "Frame/WorldTick/LiminalCapture LiminalPortal_4/SceneRender/RenderGraphExecute - LiminalCapture LiminalPortal_4/Scene"


def paths(entries):
    return [{"path": path, "ms": ms, "countPerFrame": 1.0} for path, ms in entries]


class ViewTests(unittest.TestCase):
    def test_main_and_capture(self):
        self.assertEqual(view_of(MAIN + "/BasePass"), "main")
        self.assertEqual(view_of(CAPTURE + "/BasePass"), "capture:LiminalPortal_4")

    def test_rest_of_frame_is_not_a_view(self):
        self.assertIsNone(view_of("Frame/SlateUI"))
        self.assertIsNone(view_of("Frame/WorldTick"))


class ClassifyTests(unittest.TestCase):
    def test_deepest_match_wins(self):
        # La proyección de sombra cuelga de Lights: cuenta como sombra, no como luces diferidas.
        self.assertEqual(classify_path((MAIN + "/RenderDeferredLighting/Lights/DirectLighting/L_Hub.Sun/ShadowProjectionOnOpaque").split("/")), "shadowsVsm")
        self.assertEqual(classify_path((MAIN + "/RenderDeferredLighting/Lights").split("/")), "deferredLights")
        self.assertEqual(classify_path((MAIN + "/PostProcessing/TemporalSuperResolution(sg.AntiAliasingQuality=3) 1400x788 -> 1920x1080").split("/")), "tsr")
        self.assertEqual(classify_path((MAIN + "/DiffuseIndirectAndAO/LumenScreenProbeGather").split("/")), "lumenScreenProbeGather")
        self.assertEqual(classify_path((MAIN + "/DiffuseIndirectAndAO/TranslucencyVolumeLighting").split("/")), "lumenTranslucencyVolume")

    def test_unknown_is_other(self):
        self.assertEqual(classify_path((MAIN + "/CopyStencilToLightingChannels").split("/")), "other")


class ExclusiveTests(unittest.TestCase):
    def test_children_are_subtracted(self):
        exclusive = exclusive_times(paths([("Q/A", 5.0), ("Q/A/B", 2.0), ("Q/A/C", 1.0), ("Q/A/B/D", 0.5)]))
        self.assertAlmostEqual(exclusive["Q/A"], 2.0)
        self.assertAlmostEqual(exclusive["Q/A/B"], 1.5)
        self.assertAlmostEqual(exclusive["Q/A/B/D"], 0.5)

    def test_never_negative(self):
        # Hijas que suman más que la madre (redondeo, migas repetidas): el exclusivo se queda en 0.
        exclusive = exclusive_times(paths([("Q/A", 1.0), ("Q/A/B", 1.5)]))
        self.assertEqual(exclusive["Q/A"], 0.0)


class SummarizeCaseTests(unittest.TestCase):
    def test_views_queues_and_per_capture_mean(self):
        case = {
            "name": "portals2", "captures": 2, "frames": 10, "gpuMsMean": 20.0, "fpsMean": 50.0,
            "paths": paths([
                ("Graphics0/" + MAIN, 6.0), ("Graphics0/" + MAIN + "/BasePass", 1.0),
                ("Graphics0/" + MAIN + "/PostProcessing", 3.0),
                ("Graphics0/" + MAIN + "/PostProcessing/TemporalSuperResolution 1400x788 -> 1920x1080", 2.5),
                ("Compute0/" + MAIN, 2.0), ("Compute0/" + MAIN + "/DiffuseIndirectAndAO", 2.0),
                ("Compute0/" + MAIN + "/DiffuseIndirectAndAO/LumenScreenProbeGather", 2.0),
                ("Graphics0/" + CAPTURE + "/ShadowDepths", 2.0),
                ("Graphics0/" + CAPTURE.replace("Portal_4", "Portal_6") + "/ShadowDepths", 4.0),
                ("Graphics0/Frame/SlateUI", 0.3),
            ]),
        }
        summary = summarize_case(case)
        main = summary["main"]
        self.assertAlmostEqual(main["totalMs"], 8.0)
        self.assertAlmostEqual(main["byCategory"]["basePass"], 1.0)
        self.assertAlmostEqual(main["byCategory"]["tsr"], 2.5)
        self.assertAlmostEqual(main["byCategory"]["postProcess"], 0.5)
        self.assertAlmostEqual(main["byCategory"]["lumenScreenProbeGather"], 2.0)
        self.assertAlmostEqual(main["byQueue"]["Compute0"], 2.0)
        self.assertEqual(sorted(summary["captureViews"]), ["LiminalPortal_4", "LiminalPortal_6"])
        self.assertAlmostEqual(summary["capturePerView"]["byCategory"]["shadowsVsm"], 3.0)
        # Lo que no es vista (Slate) no entra en ninguna.
        self.assertAlmostEqual(main["totalMs"] + 2 * summary["capturePerView"]["totalMs"], 14.0)

    def test_case_without_captures(self):
        summary = summarize_case({"name": "portals0", "paths": paths([("Graphics0/" + MAIN, 1.0)])})
        self.assertIsNone(summary["capturePerView"])
        self.assertEqual(summary["captureViews"], {})


if __name__ == "__main__":
    unittest.main()
