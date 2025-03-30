import subprocess
import re
from collections import defaultdict
from datetime import datetime

# Run the tcpdump command and capture the output
command = "tcpdump -r capture.pcap -tttt -q | grep tcp"
result = subprocess.run(command, shell=True, capture_output=True, text=True)

# Get the output from the command
tcpdump_output = result.stdout

# Dictionary to store flows by destination port
flows = defaultdict(list)

# Regular expression to match the required fields in the output
pattern = re.compile(r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{6}) IP \S+ > \S+\.(\d+): tcp (\d+)')

# Process each line of the output
for line in tcpdump_output.strip().split('\n'):
    match = pattern.match(line)
    if match:
        timestamp, dest_port, size = match.groups()
        size = int(size)
        if size > 0:
            flows[dest_port].append((timestamp, int(size)))

# Write the flows by destination port to a file
for port, flow in flows.items():
    with open(f'{port}.txt', 'w') as f:
        if flow:
            base_time = datetime.strptime(flow[0][0].split()[1], '%H:%M:%S.%f')
            for timestamp, size in flow:
                current_time = datetime.strptime(timestamp.split()[1], '%H:%M:%S.%f')
                time_delta = current_time - base_time
                base_time = current_time
                f.write(f"{time_delta} {size}\n")
        f.write("\n") 
