"""Generate compact resource facts from cached DofusDB /skills and /recoltables2.

Inputs are JSON arrays containing every page, sorted by id for recoltables2.
Only standard gathering professions and water are retained, as on DofusDB's map.
No image, game asset, user data or API response is embedded in the generated file.
"""
import argparse
import collections
import json
from pathlib import Path


def generate(skills, maps):
    catalog = {}
    for skill in skills:
        job, item_id = skill["parentJobId"], skill["gatheredRessourceItem"]
        if job not in (1, 2, 24, 26, 28, 36) or (job == 1 and item_id != 311):
            continue
        item = skill["item"]
        assert item_id == item["id"] and item["iconId"] > 0
        entry = (item_id, job, skill["levelMin"], item["iconId"], item["name"]["fr"])
        # Multiple gathering skills can expose the same resource.
        if item_id not in catalog or entry[2] < catalog[item_id][2]:
            catalog[item_id] = entry
    assert 1 <= len(catalog) <= 128
    placements, seen = [], set()
    for row in maps:
        map_id, pos = row["id"], row["pos"]
        assert map_id > 0 and map_id == pos["id"] and map_id not in seen
        seen.add(map_id)
        counts = collections.Counter()
        for point in row["quantities"]:
            item_id, cell, count = point["item"], point["cell"], point["quantity"]
            # Several interactive elements can occupy one cell. Preserve the
            # source's count, then cross-check it against its per-item totals.
            assert 0 <= cell < 560 and 1 <= count <= 560
            counts[item_id] += count
        assert dict(counts) == {int(k): v for k, v in row["details"].items()}
        for item_id, count in counts.items():
            if item_id not in catalog:
                continue
            assert 1 <= count <= 560 and 0 <= pos["worldMap"] <= 1000
            assert all(isinstance(pos[k], int) and -100000 <= pos[k] <= 100000 for k in ("posX", "posY"))
            placements.append((map_id, pos["worldMap"], pos["posX"], pos["posY"],
                               item_id, count, not pos["outdoor"]))
    placements.sort(key=lambda p: (p[1], p[2], p[3], p[4], p[0]))
    # A missing position must never silently remove a resource from the selector.
    resources = sorted(catalog.values(), key=lambda r: (r[1], r[2], r[0]))
    lines = ["// Generated from DofusDB public gathering facts. See docs/RESOURCE-DATA.md.",
             "#pragma once", '#include "maps/resource-map-data.hpp"',
             "namespace resourceMap::generated {", "inline constexpr Resource Resources[] = {"]
    for resource in resources:
        lines.append("    {" + ",".join(str(v) for v in resource[:4]) + "," +
                     json.dumps(resource[4], ensure_ascii=False) + "},")
    lines += ["};", "inline constexpr Placement Nodes[] = {"]
    for row in placements:
        lines.append("    {" + ",".join(str(v) for v in row[:-1]) + "," +
                     ("true" if row[-1] else "false") + "},")
    lines += ["};", "}", ""]
    return "\n".join(lines), len(resources), len(placements)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--placements", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    text, resources, placements = generate(json.loads(args.catalog.read_text(encoding="utf-8")),
                                          json.loads(args.placements.read_text(encoding="utf-8")))
    args.output.write_text(text, encoding="utf-8", newline="\n")
    print(f"{resources} resources; {placements} map/resource records")
