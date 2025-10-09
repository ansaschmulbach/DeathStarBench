import json
import subprocess

# Function to get IP mapping from Docker containers
def get_ip_mapping():
    ip_mapping = {}
    result = subprocess.run(
        "docker ps --format '{{.Names}}: {{.ID}}' | while read name id; do echo \"$name: $(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' $id)\"; done",
        shell=True,
        capture_output=True,
        text=True
    )
    for line in result.stdout.split('\n'):
        if ': ' in line:
            service, ip = line.split(': ')
            ip_mapping[service.strip()] = ip.strip()
    return ip_mapping

# Read the config and bottom text input from a file
# with open('config-zsim/service-config.json', 'r') as f:
with open('config/service-config.json', 'r') as f:
    data = f.read()

# Split the data into config and bottom text
config_data = data

# Load the config JSON
config = json.loads(config_data)

# Get IP mapping from Docker containers
ip_mapping = get_ip_mapping()

# Function to update IP addresses in the config
def update_ip_addresses(config, ip_mapping):
    for service_name, service in config.items():
        if isinstance(service, dict) and 'addr' in service:
            for key, ip in ip_mapping.items():
                if service_name in key:
                    service['addr'] = ip
    return config

# Update the IP addresses in the config
updated_config = update_ip_addresses(config, ip_mapping)

# Print the updated configuration
print(json.dumps(updated_config, indent=2))
