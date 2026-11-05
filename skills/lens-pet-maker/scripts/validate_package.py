# -*- coding: utf-8 -*-
"""Independent contract checker for a Lens pet package.

Mirrors the application's validation semantics (src/core/domain/pet/pet_config.cpp,
src/app/pet/pet_assets.cpp, pet_package_fs.cpp, pet_state.h roles). Exit 0 = the
package would load; every FAIL is a refusal reason the app would show.

Usage:
    python -I validate_package.py <package-dir>
"""
import json
import os
import re
import struct
import sys

BUNDLED_IDS = {"cat", "dog", "cloud", "ghost", "jelly", "mochi", "owl"}
CLOSED = {"idle", "study", "thinking", "celebrate", "encourage", "sleep", "click_react",
          "pickup", "look_around", "yawn", "stretch", "angry", "ask", "dizzy"}
REQUIRED = CLOSED - {"stretch", "angry", "ask", "dizzy"}

FAILS = []
WARNS = []
OKS = 0


def check(cond, label):
    global OKS
    if cond:
        OKS += 1
    else:
        FAILS.append(label)


def warn(cond, label):
    if not cond:
        WARNS.append(label)


def is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", head[16:24])


def safe_rel(path):
    if not isinstance(path, str) or not path:
        return False
    p = path.replace("\\", "/")
    if re.match(r"^[A-Za-z]:", p) or p.startswith("//") or p.startswith("\\\\"):
        return False
    return ".." not in p.split("/")


def plain_label(s):
    return (isinstance(s, str) and 0 < len(s) <= 32
            and not any(ord(c) < 32 or 0x7F <= ord(c) <= 0x9F for c in s))


def run(pkg):
    global OKS
    pkg = os.path.abspath(pkg)
    root_name = os.path.basename(pkg.rstrip("\\/"))
    print("checking", pkg)

    for name in ("pet.json", "animations.json", "anchors.json", "accessories.json"):
        check(os.path.isfile(os.path.join(pkg, name)), "missing required file " + name)
    if FAILS:
        report()
        return False

    files = []
    for dirpath, _dirnames, filenames in os.walk(pkg):
        for fn in filenames:
            files.append(os.path.join(dirpath, fn))
    check(len(files) <= 512, "file count > 512")
    check(not any(os.path.relpath(f, pkg).lower().startswith("sign")
                  or "\\sign\\" in f or "/sign/" in f for f in files), "sign/ or sign.json present")
    for f in files:
        rel = os.path.relpath(f, pkg).replace("\\", "/")
        check(".." not in rel.split("/"), "path escapes package: " + rel)
        if f.lower().endswith(".json"):
            check(os.path.getsize(f) <= 256 * 1024, "json over 256KB: " + rel)
        else:
            check(png_size(f) is not None, "not a PNG: " + rel)
            check(os.path.getsize(f) <= 8 * 1024 * 1024, "png over 8MB: " + rel)
    check(sum(os.path.getsize(f) for f in files) <= 32 * 1024 * 1024, "package over 32MB")

    def load(name):
        with open(os.path.join(pkg, name), encoding="utf-8") as f:
            return json.load(f)

    def resolve(path):
        return os.path.join(pkg, *path.replace("\\", "/").split("/"))

    # pet.json
    pet = load("pet.json")
    check(pet.get("format") == 1, "format != 1")
    pid = pet.get("id")
    check(isinstance(pid, str) and re.fullmatch(r"[a-z0-9_]{1,32}", pid or ""), "bad id")
    check(pid == root_name, "id != folder name")
    check(pid.lower() not in BUNDLED_IDS, "id collides with a bundled pet: " + str(pid))
    canvas = pet.get("canvas")
    check(is_int(canvas) and 16 <= canvas <= 128, "canvas out of range")
    scale = pet.get("scale")
    check(is_int(scale) and 1 <= scale <= 4, "scale out of range")
    layers = pet.get("layers")
    check(isinstance(layers, dict)
          and all(is_int(layers.get(k)) for k in ("body", "expression", "effect")), "layers bad")
    expr_path = pet.get("expression")
    check(safe_rel(expr_path), "expression path unsafe")
    if pet.get("name") is not None:
        check(isinstance(pet["name"], str) and len(pet["name"]) <= 32, "name too long")
    extra = set(pet) - {"format", "id", "name", "canvas", "scale", "layers", "expression"}
    warn(not extra, "pet.json has undocumented keys: " + ",".join(sorted(extra)))

    # animations.json
    anim = load("animations.json")
    roles, actions = anim.get("roles"), anim.get("actions")
    idle_pool = anim.get("idlePool")
    check(isinstance(roles, dict), "roles missing")
    check(isinstance(actions, dict), "actions missing")
    check(isinstance(idle_pool, list), "idlePool missing")

    check(set(roles) <= CLOSED, "unknown role names: " + ",".join(set(roles) - CLOSED))
    check(REQUIRED <= set(roles), "missing required roles: " + ",".join(REQUIRED - set(roles)))
    named = [k for k in actions if k != "blink"]
    check(len(named) <= 32, "more than 32 actions")
    for r, a in roles.items():
        check(isinstance(a, str) and a in actions and a != "blink", "role %s names no action" % r)

    for a, spec in actions.items():
        check(plain_label(a), "action name not plain: " + a)
        check(is_int(spec.get("frames")) and 1 <= spec["frames"] <= 64, a + ".frames")
        check(is_int(spec.get("fps")) and 1 <= spec["fps"] <= 24, a + ".fps")
        check(isinstance(spec.get("loop"), bool), a + ".loop")
        if a == "blink":
            warn("sheet" not in spec, "blink carries a sheet (ignored by app)")
            continue
        check(safe_rel(spec.get("sheet")), a + ".sheet path")
        if "effect" in spec:
            check(safe_rel(spec.get("effect")), a + ".effect path")
        if "duration" in spec:
            check(is_int(spec["duration"]) and 1 <= spec["duration"] <= 3600000, a + ".duration")
        if "returnTo" in spec:
            check(spec["returnTo"] in roles, a + ".returnTo names unmapped role")
        if "blink" in spec:
            check(isinstance(spec["blink"], bool), a + ".blink flag")
    check("blink" in actions, "blink entry missing")

    check(len(idle_pool) <= 16, "idlePool over 16")
    for it in idle_pool:
        check(it in actions and it != "blink" and actions[it].get("loop") is False,
              "idlePool entry not a one-shot: " + str(it))

    if "hover" in anim:
        hv = anim["hover"]
        for k in ("notice", "dwell"):
            if k in hv:
                check(hv[k] in actions and hv[k] != "blink"
                      and actions[hv[k]].get("loop") is False, "hover.%s not one-shot" % k)
        check("dwell" not in hv or "notice" in hv, "hover.dwell without notice")
        check(bool(hv.get("notice")) or bool(hv.get("dwell")), "hover empty")
        for k in ("noticeDelayMs", "dwellDelayMs", "cooldownMs"):
            if k in hv:
                check(is_int(hv[k]) and 0 <= hv[k] <= 60000, "hover." + k)

    if "movement" in anim:
        mv = anim["movement"]
        check(mv.get("style") in {"glide", "float", "hop", "trot", "pounce"}, "movement.style")
        check(mv.get("walk") in actions and actions[mv["walk"]].get("loop") is True,
              "movement.walk not a loop action")
        if "peek" in mv:
            check(mv["peek"] in actions and actions[mv["peek"]].get("loop") is True,
                  "movement.peek not a loop action")
        if "range" in mv:
            check(is_int(mv["range"]) and 10 <= mv["range"] <= 60, "movement.range")

    if "behavior" in anim:
        bh = anim["behavior"]
        if "idleBaseMs" in bh:
            check(is_int(bh["idleBaseMs"]) and 1000 <= bh["idleBaseMs"] <= 600000, "behavior.idleBaseMs")
        for k in ("idleSpreadMs", "angryWindowMs", "wanderWaitMinMs", "wanderWaitMaxMs"):
            if k in bh:
                check(is_int(bh[k]) and 0 <= bh[k] <= 600000, "behavior." + k)
        if "wanderWaitMinMs" in bh and "wanderWaitMaxMs" in bh:
            check(bh["wanderWaitMinMs"] <= bh["wanderWaitMaxMs"], "wanderWaitMin > Max")
        for k in ("sceneChance", "napChance", "hideChance"):
            if k in bh:
                check(isinstance(bh[k], (int, float)) and not isinstance(bh[k], bool)
                      and 0 <= bh[k] <= 1, "behavior." + k)
        if "angryClicks" in bh:
            check(is_int(bh["angryClicks"]) and 1 <= bh["angryClicks"] <= 10, "behavior.angryClicks")
        if "strollSteps" in bh:
            check(is_int(bh["strollSteps"]) and 1 <= bh["strollSteps"] <= 4, "behavior.strollSteps")
        if "gazeRadius" in bh:
            check(is_int(bh["gazeRadius"]) and 21 <= bh["gazeRadius"] <= 128, "behavior.gazeRadius")

    # sheets
    expr_size = png_size(resolve(expr_path))
    check(expr_size is not None and expr_size[1] == canvas and expr_size[0] % canvas == 0,
          "expression sheet size")
    cells = expr_size[0] // canvas if expr_size else 0
    check(cells <= 64, "expression cells > 64")
    total_px = expr_size[0] * expr_size[1] if expr_size else 0

    from PIL import Image  # noqa: late import; Pillow is a skill dependency
    blank = []
    for a, spec in actions.items():
        if a == "blink":
            for i in spec.get("expressions", []):
                check(is_int(i) and 0 <= i < cells, "blink expression index " + str(i))
            continue
        w, h = png_size(resolve(spec["sheet"]))
        check((w, h) == (spec["frames"] * canvas, canvas), "%s body sheet %s" % (a, (w, h)))
        total_px += w * h
        img = Image.open(resolve(spec["sheet"])).convert("RGBA")
        for i in range(spec["frames"]):
            if img.crop((i * canvas, 0, (i + 1) * canvas, canvas)).getchannel("A").getbbox() is None:
                blank.append("%s#%d" % (a, i))
        if "effect" in spec:
            we, he = png_size(resolve(spec["effect"]))
            check((we, he) == (w, h), "%s effect sheet" % a)
            total_px += we * he
        for i in spec.get("expressions", []):
            check(is_int(i) and 0 <= i < cells, a + " expression index " + str(i))
        el = len(spec.get("expressions", []))
        warn(el <= spec["frames"], "%s expressions longer than frames" % a)
    check(not blank, "blank frames: " + ",".join(blank))
    check(total_px <= 16_000_000, "total pixels over 16M")

    # anchors
    anch = load("anchors.json")["actions"]
    check("blink" not in anch, "blink listed in anchors (refused)")
    check(set(anch) == set(named), "anchors keys != actions: " + ",".join(set(anch) ^ set(named)))
    for a, entries in anch.items():
        check(len(entries) == actions[a]["frames"], "anchors[%s] count != frames" % a)
        for e in entries:
            ok = isinstance(e, dict) and set(e) >= {"head", "face", "body"}
            if ok:
                for k in ("head", "face", "body"):
                    p = e[k]
                    ok = ok and isinstance(p, list) and len(p) == 2 and all(is_int(v) for v in p)
            check(ok, "anchors[%s] malformed" % a)

    # idle frame 1 sanity: ink present and feet-ish
    if "idle" in roles:
        first = anch[roles["idle"]][0]
        for k in ("head", "face", "body"):
            check(list(first.get(k, [1, 1])) == [0, 0], "idle frame 1 %s anchor not [0,0]" % k)
        idle_img = Image.open(resolve(actions[roles["idle"]]["sheet"])).convert("RGBA")
        bbox = idle_img.crop((0, 0, canvas, canvas)).getchannel("A").getbbox()
        check(bbox is not None, "idle frame 1 is empty")
        if bbox:
            check(bbox[3] >= canvas * 2 // 3, "idle frame 1 feet not near canvas bottom")
            warn((bbox[2] - bbox[0]) >= canvas // 3, "idle frame 1 figure very narrow")

    # accessories
    accs = load("accessories.json")["accessories"]
    check(isinstance(accs, list), "accessories array missing")
    check(len(accs) <= 32, "more than 32 accessories")
    ids = set()
    for acc in accs:
        aid = acc.get("id")
        check(plain_label(aid), "accessory id not plain")
        check(aid not in ids, "accessory id repeated: " + str(aid))
        ids.add(aid)
        check(acc.get("slot") in {"head", "face", "body"}, "accessory slot: " + str(acc.get("slot")))
        ap = acc.get("asset")
        check(safe_rel(ap), "accessory asset path: " + str(ap))
        aw, ah = png_size(resolve(ap)) if safe_rel(ap) else (None, None)
        check((aw, ah) == (canvas, canvas), "accessory asset size: " + str(ap))
        check(is_int(acc.get("zIndex")), "accessory zIndex: " + str(aid))
        sa = acc.get("supportedActions")
        check(isinstance(sa, list) and all(x in actions for x in sa),
              "accessory supportedActions: " + str(aid))
        sw = acc.get("sceneWeights")
        if sw is not None:
            check(isinstance(sw, dict) and len(sw) <= 8, "sceneWeights entries: " + str(aid))
            for k, v in sw.items():
                check(k in actions, "sceneWeights key not an action: " + str(k))
                check(isinstance(v, (int, float)) and not isinstance(v, bool) and 0 < v <= 4,
                      "sceneWeights value: " + str(v))
    warn(not any("sceneWeights" in a for a in accs), "sceneWeights present (inert for packages)")

    return report()


def report():
    print("PASS %d checks" % OKS)
    for w in WARNS:
        print("WARN  " + w)
    for f in FAILS:
        print("FAIL  " + f)
    print("RESULT: " + ("OK" if not FAILS else "REFUSED"))
    return not FAILS


if __name__ == "__main__":
    ok = run(sys.argv[1])
    sys.exit(0 if ok else 1)
