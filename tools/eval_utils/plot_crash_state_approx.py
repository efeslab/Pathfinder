import matplotlib.pyplot as plt
import numpy as np

FONT_SIZE = 14
# Data provided by the user
data = {
    "Memcached": {
        "0": {
            "exhaustive": [7705, 1415],
            "rep": [217, 207]
        },
        "1": {
            "exhaustive": [19, 1],
            "rep": [4, 7]
        },
        "2": {
            "exhaustive": [19, 1],
            "rep": [10, 6]
        }
    },
    "HSE": {
        "0": {
            "exhaustive": [0, 2230],
            "rep": [2, 42]
        },
        "1": {
            "exhaustive": [0, 2184],
            "rep": [2, 42]
        }
    },
    "Redis": {
        "0": {
            "exhaustive": [1076, 2832],
            "rep": [661, 146]
        }
    }
}

# Prepare the data
benchmarks = []
exhaustive_data = []
rep_data = []

for benchmark, bugs in data.items():
    for bug, values in bugs.items():
        benchmarks.append(f"{benchmark} bug {bug}")
        exhaustive = values["exhaustive"]
        rep = values["rep"]
        
        exhaustive_total = sum(exhaustive)
        rep_total = sum(rep)
        
        exhaustive_ratio = [1]
        rep_ratio = [rep_total / exhaustive_total]
        print(rep_ratio)
        exhaustive_data.append(exhaustive_ratio)
        rep_data.append(rep_ratio)

# Bar plot
bar_width = 0.4
index = np.arange(len(benchmarks))

fig, ax = plt.subplots(figsize=(14, 8))

# Plot exhaustive bars
for i in range(len(exhaustive_data)):
    ax.bar(index[i] - bar_width/2, exhaustive_data[i], bar_width, label=f"Exhaustive {benchmarks[i]}" if i == 0 else "", color='cyan', edgecolor='black')
    
# Plot rep bars
for i in range(len(rep_data)):
    ax.bar(index[i] + bar_width/2, rep_data[i], bar_width, label=f"Rep {benchmarks[i]}" if i == 0 else "", color='gold', edgecolor='black')

ax.set_xlabel('Benchmarks')
ax.set_ylabel('Normalized Values')
ax.set_title('Normalized Crash States Tested for Each Tool on Benchmark for Each Bug')
ax.set_xticks(index)
ax.set_xticklabels(benchmarks, rotation=0, ha="right")
ax.legend()

plt.tight_layout()
# plt.show()
fig.savefig("crash_state_approx.png")
