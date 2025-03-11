import re
import subprocess

# Function to parse log data from a file and create a summary file
def parse_docker_logs(input_file, output_file):
    metrics = {}
    
    # Get the docker logs
    result = subprocess.run(['docker', 'logs', service_name], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    # log_data = result.stdout.decode('utf-8') + result.stderr.decode('utf-8')
    log_data = result.stderr.decode('utf-8')
    for line in log_data.strip().split('\n'):
        match = re.search(r'\[perf\]\s+([\w-]+):\s+([\d.e+-]+)', line)
        if match:
            metric_name = match.group(1)
            metric_value = match.group(2)
            if metric_name not in metrics:
                metrics[metric_name] = []
            else:
                metrics[metric_name].append(int(float(metric_value)))
    
    for metric_name, values in metrics.items():
        value = sum(values)/len(values)
        print(f"{metric_name}: {value}")

# Specify the service name and output file name
service_name = 'socialnetwork-compose-post-service-1'
output_file = 'summary.txt'

# Call the function to parse the docker logs and create the summary file
parse_docker_logs(service_name, output_file)
