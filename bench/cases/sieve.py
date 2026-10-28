# 埃氏筛：数组下标读写
n = 5000000
is_prime = [True] * (n + 1)
is_prime[0] = False
is_prime[1] = False
i = 2
while i * i <= n:
    if is_prime[i]:
        j = i * i
        while j <= n:
            is_prime[j] = False
            j += i
    i += 1
count = 0
for k in range(n + 1):
    if is_prime[k]:
        count += 1
print(count)
