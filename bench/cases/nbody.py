# 结构体字段读写与浮点运算：N 体引力模拟（5 个天体）
import math
class Body:
    __slots__ = ("x", "y", "z", "vx", "vy", "vz", "mass")
    def __init__(self, x, y, z, vx, vy, vz, mass):
        self.x, self.y, self.z = x, y, z
        self.vx, self.vy, self.vz = vx, vy, vz
        self.mass = mass
SM = 4 * math.pi * math.pi
D = 365.24
bodies = [
    Body(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, SM),
    Body(4.841431442464721, -1.1603200440274284, -0.10362204447112311,
         0.001660076642744037 * D, 0.007699011184197404 * D, -0.0000690460016972063 * D, 0.0009547919384243266 * SM),
    Body(8.34336671824458, 4.124798564124305, -0.4035234171143214,
         -0.002767425107268624 * D, 0.004998528012349172 * D, 0.000023041729757376393 * D, 0.0002858859806661308 * SM),
    Body(12.894369562139131, -15.111151401698631, -0.22330757889265573,
         0.002964601375647616 * D, 0.0023784717395948095 * D, -0.000029658956854023756 * D, 0.00004366244043351563 * SM),
    Body(15.379697114850917, -25.919314609987964, 0.17925877295037118,
         0.002680677724903893 * D, 0.001628241700382423 * D, -0.00009515922545197159 * D, 0.000051513890204661145 * SM),
]
def energy():
    e = 0.0
    n = len(bodies)
    for i in range(n):
        b = bodies[i]
        e += 0.5 * b.mass * (b.vx * b.vx + b.vy * b.vy + b.vz * b.vz)
        for j in range(i + 1, n):
            c = bodies[j]
            dx = b.x - c.x
            dy = b.y - c.y
            dz = b.z - c.z
            e -= b.mass * c.mass / math.sqrt(dx * dx + dy * dy + dz * dz)
    return e
def advance(dt):
    n = len(bodies)
    for i in range(n):
        b = bodies[i]
        for j in range(i + 1, n):
            c = bodies[j]
            dx = b.x - c.x
            dy = b.y - c.y
            dz = b.z - c.z
            d2 = dx * dx + dy * dy + dz * dz
            mag = dt / (d2 * math.sqrt(d2))
            bm = b.mass * mag
            cm = c.mass * mag
            b.vx -= dx * cm
            b.vy -= dy * cm
            b.vz -= dz * cm
            c.vx += dx * bm
            c.vy += dy * bm
            c.vz += dz * bm
    for b in bodies:
        b.x += dt * b.vx
        b.y += dt * b.vy
        b.z += dt * b.vz
print("%.9f" % energy())
for step in range(100000):
    advance(0.01)
print("%.9f" % energy())
