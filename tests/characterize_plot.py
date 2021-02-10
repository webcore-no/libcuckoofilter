#!/bin/python
import matplotlib.pyplot as plt
import csv
import sys

x=[]
y=[]

plots= csv.reader(sys.stdin, delimiter=',')
for row in plots:
    x.append(int(row[0]))
    y.append(int(row[1]))


plt.plot(x,y, marker='o')

plt.title('Duplicates over time')

plt.xlabel('Elements')
plt.ylabel('Duplicates')

plt.show()
