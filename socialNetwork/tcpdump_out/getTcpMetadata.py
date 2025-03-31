import subprocess
import os
import glob
import re
from collections import defaultdict
from datetime import datetime

# Get a list of all .pcap files in the current directory
pcap_files = glob.glob("*.pcap")

for pcap_file in pcap_files:

    # Remove the .pcap extension for directory name
    dir_name = os.path.splitext(pcap_file)[0]
    
    # Create the directory if it doesn't exist
    if not os.path.exists(dir_name):
        os.makedirs(dir_name)
        print(f"Created directory: {dir_name}")

    # Run the tcpdump command and capture the output
    command = f"tcpdump -r {pcap_file} -tttt -q | grep tcp"
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
        with open(os.path.join(dir_name, f'{port}.txt'), 'w') as f:
            if flow:
                base_time = datetime.strptime(flow[0][0].split()[1], '%H:%M:%S.%f')
                for timestamp, size in flow:
                    current_time = datetime.strptime(timestamp.split()[1], '%H:%M:%S.%f')
                    # time_delta = current_time - base_time
                    # base_time = current_time
                    f.write(f"{current_time} {size}\n")
            f.write("\n") 

for pcap_file in pcap_files:

    dir_name = os.path.splitext(pcap_file)[0]
    tcpflow_command = f"tcpflow -r {pcap_file} -o {dir_name}"
    result = subprocess.run(tcpflow_command, shell=True, capture_output=True, text=True)
    
    # Check if tcpflow executed successfully
    if result.returncode == 0:
        print(f"tcpflow completed successfully for {pcap_file}. Output saved to {dir_name}")
    else:
        print(f"Error running tcpflow on {pcap_file}: {result.stderr}")
