# 方法调用：面向对象风格的计数器
class Counter:
    def __init__(self, value=0, step=1):
        self.value = value
        self.step = step
    def tick(self):
        self.value += self.step
    def get(self):
        return self.value
cs = [Counter(), Counter(step=2), Counter(step=3)]
for i in range(1000000):
    cs[i % 3].tick()
print(cs[0].get(), cs[1].get(), cs[2].get())
