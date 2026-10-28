# 递归快速排序（手写，不用内建 sort），数据由线性同余生成器产生
import sys
sys.setrecursionlimit(10000)
seed = 12345
def next_rand():
    global seed
    seed = (seed * 1103515245 + 12345) % 2147483648
    return seed
def quicksort(a, lo, hi):
    if lo >= hi:
        return
    pivot = a[(lo + hi) // 2]
    i = lo
    j = hi
    while i <= j:
        while a[i] < pivot:
            i += 1
        while a[j] > pivot:
            j -= 1
        if i <= j:
            t = a[i]
            a[i] = a[j]
            a[j] = t
            i += 1
            j -= 1
    quicksort(a, lo, j)
    quicksort(a, i, hi)
n = 300000
data = []
for k in range(n):
    data.append(next_rand() % 1000000)
quicksort(data, 0, n - 1)
ok = True
for k in range(1, n):
    if data[k - 1] > data[k]:
        ok = False
print("true" if ok else "false", data[0], data[n // 2], data[n - 1])
