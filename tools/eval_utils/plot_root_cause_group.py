import matplotlib.pyplot as plt
import numpy as np

data = {
    "level_hashing": {
        "rep": {'0': 499, '1': 342, '7': 177, '15': 91, '18': 82, '26': 62, '32': 55, '24': 75, '31': 27, '11': 15, '6': 102, '12': 32, '21': 38, '16': 73, '23': 89, '4': 25, '14': 43, '25': 35, '8': 31, '27': 14, '2': 43, '10': 14, '34': 59, '5': 28, '19': 4, '3': 26, '22': 4, '13': 18, '20': 6, '28': 14, '29': 6, '9': 13, '30': 8, '17': 11, '33': 12},
        "induced": {'1': 2526, '0': 4386, '25': 63, '23': 628, '16': 571, '29': 45, '7': 2499, '15': 200, '6': 233, '5': 150, '14': 185, '8': 162, '32': 34, '26': 36, '2': 54, '12': 16, '10': 20, '30': 21, '21': 23, '34': 108, '31': 19, '24': 366, '18': 77, '19': 19, '11': 17, '33': 49, '17': 15, '27': 31, '9': 42, '22': 18, '28': 7, '4': 34, '13': 7, '3': 42, '20': 10},
        "exhaustive": {'0': 4587289, '15': 2431, '7': 34988, '6': 658, '23': 7366, '5': 956, '14': 1290, '31': 9019, '8': 2376, '16': 4792, '26': 448, '24': 1469, '32': 3802, '1': 64962, '12': 170, '34': 511, '29': 270, '2': 476, '28': 28, '13': 26, '21': 179, '11': 112, '27': 19, '9': 30, '33': 28, '18': 334, '25': 358, '30': 218, '4': 58, '10': 40, '17': 75, '3': 80, '22': 12, '20': 2, '19': 1},
    }
}

rep_data = data["level_hashing"]["rep"]
induced_data = data["level_hashing"]["induced"]
exhaustive_data = data["level_hashing"]["exhaustive"]

# Extracting keys and corresponding values
keys = sorted(rep_data.keys(), key=int)
keys = keys[1:]
rep_values = [rep_data[key] for key in keys]
induced_values = [induced_data[key] for key in keys]
exhaustive_values = [exhaustive_data[key] for key in keys]

rep_exhaustive_ratio = [rep_data[key] / exhaustive_data[key] for key in keys]
# induced_exhaustive_ratio = [induced_data[key] / exhaustive_data[key] for key in keys]
print(rep_exhaustive_ratio)
# print(induced_exhaustive_ratio)
for i in range(len(keys)):
    if rep_exhaustive_ratio[i] > 1.0:
        print(keys[i], rep_values[i], induced_values[i], exhaustive_values[i])

x = np.arange(len(keys))  # the label locations
width = 0.2  # the width of the bars

fig, ax = plt.subplots(figsize=(14, 8))
bars1 = ax.bar(x - width, rep_values, width, label='Rep')
bars2 = ax.bar(x, induced_values, width, label='Induced')
bars3 = ax.bar(x + width, exhaustive_values, width, label='Exhaustive')

# Add some text for labels, title and custom x-axis tick labels, etc.
ax.set_xlabel('Keys')
ax.set_ylabel('Values')
ax.set_title('Values by key and category')
ax.set_xticks(x)
ax.set_xticklabels(keys, rotation=90)
ax.legend()

fig.tight_layout()

# plt.show()
fig.savefig("plot.png")