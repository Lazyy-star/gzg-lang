# 整数算术与嵌套循环
total = 0
for i in range(3000):
    for j in range(1000):
        total = (total + i * j + (i ^ j)) % 1000000007
print(total)
