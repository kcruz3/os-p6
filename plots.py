import pandas as pd
import matplotlib.pyplot as plt

# Sample data
configs = [8, 16, 32, 64, 128, 256]
disk_reads = [7968] * len(configs)
disk_writes = [2096] * len(configs)
flash_reads = disk_reads
flash_writes = disk_writes
flash_erases = [2096 + (2 * db) // 4 for db in configs]  # pages_per_block = 4

df = pd.DataFrame({
    'Disk Blocks': configs,
    'Disk Reads': disk_reads,
    'Disk Writes': disk_writes,
    'Flash Reads': flash_reads,
    'Flash Writes': flash_writes,
    'Flash Erases': flash_erases,
})

print(df)  # optional: print table in terminal

# Plotting
metrics = ['Disk Reads', 'Disk Writes', 'Flash Reads', 'Flash Writes', 'Flash Erases']
for metric in metrics:
    plt.figure()
    plt.plot(df['Disk Blocks'], df[metric], marker='o')
    plt.xlabel('Number of Disk Blocks')
    plt.ylabel(metric)
    plt.title(f'{metric} vs Disk Blocks')
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(f"{metric.replace(' ', '_').lower()}.png")  # save plot as .png
    plt.show()
