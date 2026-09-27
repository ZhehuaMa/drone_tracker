"""重复计算固定参考图与本项目 HUD 的 720×480 区域差异。需 Pillow。"""

from pathlib import Path
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "reference"
OUTPUT = ROOT / "comparison"
REGIONS = {
    "标题与副标题": (0, 0, 250, 90),
    "左侧遥测": (0, 110, 275, 310),
    "右侧遥测": (490, 0, 720, 310),
    "返航箭头与距离": (270, 30, 450, 125),
    "准星与俯仰梯": (270, 140, 450, 345),
    "电量条": (0, 315, 275, 375),
    "底部信息": (0, 380, 720, 480),
}


def compare(reference_name: str, actual_name: str, background: int) -> None:
    reference = Image.open(REFERENCE / reference_name).convert("RGB")
    actual = Image.open(OUTPUT / actual_name).convert("RGBA")
    base = Image.new("RGBA", actual.size, (background, background, background, 255))
    base.alpha_composite(actual)
    actual_rgb = base.convert("RGB")
    difference = ImageChops.difference(reference, actual_rgb)
    difference.save(OUTPUT / f"diff-{actual_name}")
    print(f"{reference_name} ↔ {actual_name}")
    for name, area in REGIONS.items():
        region = difference.crop(area)
        changed = sum(pixel != (0, 0, 0) for pixel in region.getdata())
        total = region.width * region.height
        print(f"  {name}: {changed}/{total} ({100 * changed / total:.2f}%)")


if __name__ == "__main__":
    compare("layout_hud_100.png", "hud-normal.png", 100)
    compare("layout_hud_150.png", "hud-alert.png", 100)
    compare("layout_det_100.png", "target-normal.png", 20)
    compare("layout_det_150.png", "target-moved.png", 20)
