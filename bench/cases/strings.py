# 字符串：插值构造、拼接、split/join、查找
parts = []
for i in range(200000):
    parts.append(f"item{i}")
text = ",".join(parts)
fields = text.split(",")
total = 0
for f in fields:
    if f.endswith("7"):
        total += len(f)
s = ""
for i in range(20000):
    s += "ab"
print(len(text), len(fields), total, len(s), text.find("item199999"))
