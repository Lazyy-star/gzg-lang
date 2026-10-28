# 闭包与高阶函数：map / filter / reduce
from functools import reduce
def make_adder(k):
    return lambda x: x + k
add3 = make_adder(3)
data = list(range(300000))
total = 0
for round in range(5):
    r = reduce(lambda a, b: a + b, filter(lambda x: x % 3 == 0, map(add3, data)), 0)
    total += r
print(total)
