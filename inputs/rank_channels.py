import glob
rows = []
for c in sorted(glob.glob("*/*/*/datacard_sanitized.txt")):
    rate = None
    binname = None
    for line in open(c):
        t = line.split()
        if not t:
            continue
        if t[0] == "bin" and len(t) == 2:
            binname = t[1]
        if t[0] == "rate":
            rate = sum(float(x) for x in t[1:])
    rows.append((rate, binname, c))
rows.sort(reverse=True)
print("%15s  %-24s %s" % ("total expected", "channel", "path"))
for r, b, c in rows[:12]:
    print("%15.1f  %-24s %s" % (r, b, c))
print("... %d channels total" % len(rows))
