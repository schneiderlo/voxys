"""Camera conventions shared by the browser and native piece thumbnails."""
import unittest

from generate_piece_thumbnails import camera_basis, dot


class ThumbnailOrientationTest(unittest.TestCase):
    def test_top_of_piece_projects_above_its_base(self):
        _, _, up = camera_basis()
        # SVG and native HUD coordinates both increase downward.
        base_y = -dot((0, 0, 0), up)
        top_y = -dot((0, 1, 0), up)
        self.assertLess(top_y, base_y)

    def test_camera_axes_are_orthonormal(self):
        camera, right, up = camera_basis()
        for axis in (camera, right, up):
            self.assertAlmostEqual(dot(axis, axis), 1)
        for a, b in ((camera, right), (camera, up), (right, up)):
            self.assertAlmostEqual(dot(a, b), 0)


if __name__ == '__main__':
    unittest.main()
