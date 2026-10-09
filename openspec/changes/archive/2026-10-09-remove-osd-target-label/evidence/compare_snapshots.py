"""比较本次软件布局快照，并在同一合成底图上生成前后对照。"""

import json
from pathlib import Path

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parent
WIDTH, HEIGHT = 720, 480


def load(folder, name):
    return np.asarray(Image.open(ROOT / folder / name).convert("RGBA"))


def bounds(mask):
    y, x = np.nonzero(mask)
    if not len(x):
        return None
    return [int(x.min()), int(y.min()), int(x.max() + 1), int(y.max() + 1)]


def compose(base, hud, target):
    return Image.alpha_composite(Image.alpha_composite(base, Image.fromarray(hud)), Image.fromarray(target))


y, x = np.indices((HEIGHT, WIDTH))
# 固定渐变与网格作为软件底图，便于看出原面板位置恢复的内容。
grid = ((x // 40 + y // 40) % 2) * 12
rgb = np.stack((30 + x * 55 // WIDTH + grid, 35 + y * 65 // HEIGHT + grid,
                45 + (x + y) * 45 // (WIDTH + HEIGHT) + grid), axis=-1).astype(np.uint8)
base = Image.fromarray(rgb).convert("RGBA")
base.save(ROOT / "synthetic-base.png")
report = {}

for name, hud_name, target_name, plate in [
    ("normal", "hud-normal.png", "target-normal.png", (459, 229, 140, 38)),
    ("alert", "hud-alert.png", "target-moved.png", (512, 154, 140, 38)),
]:
    before_hud, after_hud = load("before", hud_name), load("after", hud_name)
    before_target, after_target = load("before", target_name), load("after", target_name)
    assert np.array_equal(before_hud, after_hud), "非目标 HUD 图层发生变化"
    changed = np.any(before_target != after_target, axis=2)
    allowed = np.zeros((HEIGHT, WIDTH), dtype=bool)
    px, py, pw, ph = plate
    allowed[py:py + ph, px:px + pw] = True
    assert not np.any(changed & ~allowed), "目标装饰在原标签范围外发生变化"
    assert np.any(changed), "目标标签未移除"
    assert not np.any(after_target[py:py + ph - 4, px:px + pw, 3]), "原标签上部仍有文字或底板"
    before = compose(base, before_hud, before_target)
    after = compose(base, after_hud, after_target)
    visible_changed = np.any(np.asarray(before) != np.asarray(after), axis=2)
    assert not np.any(visible_changed & ~allowed), "合成画面在原标签范围外发生变化"
    restored = allowed & (after_target[:, :, 3] == 0)
    underlying = np.asarray(Image.alpha_composite(base, Image.fromarray(after_hud)))
    assert np.array_equal(np.asarray(after)[restored], underlying[restored]), "标签原覆盖区域未恢复底层内容"
    before.save(ROOT / f"{name}-before.png")
    after.save(ROOT / f"{name}-after.png")
    pair = Image.new("RGBA", (WIDTH * 2, HEIGHT))
    pair.paste(before, (0, 0))
    pair.paste(after, (WIDTH, 0))
    pair.convert("RGB").save(ROOT / f"{name}-comparison.png")
    difference = Image.fromarray(np.where(visible_changed[:, :, None],
                                         np.array([255, 70, 200]), np.array([20, 24, 30])).astype(np.uint8))
    difference.save(ROOT / f"{name}-difference.png")
    report[name] = {
        "HUD变化像素数": int(np.count_nonzero(np.any(before_hud != after_hud, axis=2))),
        "目标层变化像素数": int(changed.sum()),
        "合成画面变化像素数": int(visible_changed.sum()),
        "变化边界_左上及右下不含端点": bounds(visible_changed),
        "原标签范围_xywh": list(plate),
        "原标签范围外变化像素数": int((visible_changed & ~allowed).sum()),
        "底层内容恢复像素数": int(restored.sum()),
    }

assert np.array_equal(load("before", "hud-frozen.png"), load("after", "hud-frozen.png")), "冻结 HUD 发生变化"
report["frozen"] = {"HUD变化像素数": 0}
(ROOT / "pixel-diff.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
print(json.dumps(report, ensure_ascii=False, indent=2))
