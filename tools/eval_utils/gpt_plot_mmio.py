import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import MaxNLocator

# Load the checkmark image
checkmark = plt.imread('/home/yilegu/squint/pm-cc-bug-finder/tools/bug_graphs/plots/checkmark.png')
crossmark = plt.imread('/home/yilegu/squint/pm-cc-bug-finder/tools/bug_graphs/plots/crossmark.png')

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

colors = ['#99CCFF', '#CCCCCC']

# Data
labels = ['RepTest', 'Exhaustive']

# Create subplots
fig, axs = plt.subplots(3, 1, figsize=(8, 3.3))

# POSIX-based Applications
# axs[0].set_title('Correlated Crash States Tested for POSIX-based Applications', fontsize=12, fontweight='bold')

# axs[0].set_title('Correlated Crash States Tested for MMIO-based Applications', fontsize=12, fontweight='bold')

cur_ax = 0
for i, (benchmark, bugs) in enumerate(data.items()):
    rep_value = 0
    exhaustive_value = 0
    bug_text = ''
    for j, (bug, values) in enumerate(bugs.items()):
        rep_value += sum(values['rep'])
        exhaustive_value += sum(values['exhaustive'])
        if j == 0:
            bug_text += f"{bug}"
        else:
            bug_text += f",{bug}"
    print(f"{benchmark}: {rep_value}, {exhaustive_value}")
    reduction_ratio = int((1 - rep_value / exhaustive_value) * 100)
    text_start = rep_value + exhaustive_value/20
    image_width = exhaustive_value/10
    text_end = exhaustive_value
    if cur_ax == 0:
        axs[cur_ax].text((text_start + text_end) / 2, 0.1, f"reduce correlated crash states tested by {reduction_ratio}%", ha='center', fontsize=10, fontweight='bold')
    else:
        axs[cur_ax].text((text_start + text_end) / 2, 0.1, f"reduce by {reduction_ratio}%", ha='center', fontsize=10, fontweight='bold')
    axs[cur_ax].barh(labels, [rep_value, exhaustive_value], color=colors, edgecolor='black', linewidth=1.5)
    axs[cur_ax].annotate('', xy=(exhaustive_value, 0),  xytext=(text_start, 0),  
                    arrowprops=dict(arrowstyle='<->', color='black', lw=1.5))
    # axs[cur_ax].set_xlim(0, int(exhaustive_value * 1.1))
    axs[cur_ax].xaxis.set_major_locator(MaxNLocator(integer=True))
    # axs[cur_ax].imshow(checkmark, aspect='auto', extent=(rep_value, text_start, -image_width/2, image_width/2))
    # Add text at the right side of each bar plot
    axs[cur_ax].text(exhaustive_value * 1.18, 0.5, f"{benchmark} \nBug {bug_text}", va='center', ha='center', fontsize=12)
    cur_ax += 1


# # Plot RocksDB bug 0
# rep_value = 2
# exhaustive_value = 80
# reduction_ratio = int((1 - rep_value / exhaustive_value) * 100)
# text_start = rep_value + exhaustive_value/10
# text_end = exhaustive_value
# axs[0].text((text_start + text_end) / 2, 0.1, f" reduce crash states by {reduction_ratio}%", ha='center', fontsize=10, fontweight='bold')
# axs[0].barh(labels, [rep_value, exhaustive_value], color=colors, edgecolor='black', linewidth=1.5)
# axs[0].annotate('', xy=(exhaustive_value, 0), xytext=(rep_value + exhaustive_value/10, 0), 
#                 arrowprops=dict(arrowstyle='<->', color='black', lw=1.5))
# axs[0].set_xlim(0, int(exhaustive_value * 1.1))
# axs[0].xaxis.set_major_locator(MaxNLocator(integer=True))

# # Plot XXX bug 0
# rep_value = 3
# exhaustive_value = 40
# reduction_ratio = int((1 - rep_value / exhaustive_value) * 100)
# text_start = rep_value + exhaustive_value/10
# text_end = exhaustive_value
# axs[1].text((text_start + text_end) / 2, 0.1, f"reduce by {reduction_ratio}%", ha='center', fontsize=10, fontweight='bold')
# axs[1].barh(labels, [rep_value, exhaustive_value], color=colors, edgecolor='black', linewidth=1.5)
# axs[1].annotate('', xy=(exhaustive_value, 0),  xytext=(rep_value + exhaustive_value/10, 0),  
#                 arrowprops=dict(arrowstyle='<->', color='black', lw=1.5))
# axs[1].set_xlim(0, int(exhaustive_value * 1.1))
# axs[1].xaxis.set_major_locator(MaxNLocator(integer=True))

# # MMIO-based Applications
# # Plot Memcached bug 0
# axs[2].set_title('Crash State Reduction for MMIO-based Applications', fontsize=12, fontweight='bold')
# rep_value = sum(data['Memcached']['0']['rep'])
# exhaustive_value = sum(data['Memcached']['0']['exhaustive'])
# reduction_ratio = int((1 - rep_value / exhaustive_value) * 100)
# text_start = rep_value + exhaustive_value/10
# text_end = exhaustive_value
# axs[2].text((rep_value + exhaustive_value) / 2, 0.1, f"reduce by {reduction_ratio}%", ha='center', fontsize=10, fontweight='bold')
# axs[2].barh(labels, [rep_value, exhaustive_value], color=colors, edgecolor='black', linewidth=1.5)
# axs[2].annotate('', xy=(exhaustive_value, 0),  xytext=(rep_value + exhaustive_value/10, 0),  
#                 arrowprops=dict(arrowstyle='<->', color='black', lw=1.5))
# axs[2].set_xlim(0, int(exhaustive_value * 1.1))
# axs[2].xaxis.set_major_locator(MaxNLocator(integer=True))

# # Plot Memcached bug 1
# rep_value = sum(data['Memcached']['1']['rep'])
# exhaustive_value = sum(data['Memcached']['1']['exhaustive'])
# reduction_ratio = int((1 - rep_value / exhaustive_value) * 100)
# axs[3].text((rep_value + exhaustive_value) / 2, 0.1, f"reduce by {reduction_ratio}%", ha='center', fontsize=10, fontweight='bold')
# axs[3].barh(labels, [rep_value, exhaustive_value], color=colors, edgecolor='black', linewidth=1.5)
# axs[3].annotate('', xy=(exhaustive_value, 0),  xytext=(rep_value + exhaustive_value/10, 0),  
#                 arrowprops=dict(arrowstyle='<->', color='black', lw=1.5))
# axs[3].set_xlim(0, int(exhaustive_value * 1.1))
# axs[3].xaxis.set_major_locator(MaxNLocator(integer=True))

# # Plot HSE bug 0
# rep_value = sum(data['HSE']['0']['rep'])
# exhaustive_value = sum(data['HSE']['0']['exhaustive'])
# reduction_ratio = int((1 - rep_value / exhaustive_value) * 100)
# axs[4].text((rep_value + exhaustive_value) / 2, 0.1, f"reduce by {reduction_ratio}%", ha='center', fontsize=10, fontweight='bold')
# axs[4].barh(labels, [rep_value, exhaustive_value], color=colors, edgecolor='black', linewidth=1.5)
# axs[4].annotate('', xy=(exhaustive_value, 0),  xytext=(rep_value + exhaustive_value/10, 0),  
#                 arrowprops=dict(arrowstyle='<->', color='black', lw=1.5))
# axs[4].set_xlim(0, int(exhaustive_value * 1.1))
# axs[4].xaxis.set_major_locator(MaxNLocator(integer=True))


# Add grid lines
for ax in axs:
    ax.grid(True, linestyle='--', alpha=0.5)
    ax.yaxis.grid(False)
    ax.tick_params(axis='y', labelsize=12)

# Adjust layout
plt.tight_layout()
fig.savefig('crash_states_mmio.pdf')
plt.show()
