import yaml
import os

def create_bash_script(service_name, entrypoint, script_dir):
    script_content = f"""#!/bin/bash
exec {entrypoint} &
sleep 120 
tcpdump -i eth0 -w data.pcap &
TCPDUMP_PID=$!
sleep 60
kill $TCPDUMP_PID
wait
"""
    script_path = os.path.join(script_dir, f"{service_name}.sh")
    with open(script_path, 'w') as script_file:
        script_file.write(script_content)
    os.chmod(script_path, 0o755)
    return script_path

def process_services(services, script_dir):
    for service_name, service in services.items():
        if service_name.endswith("service"):
            entrypoint = service.get('entrypoint', service_name.replace("-", "").capitalize())
            script_path = create_bash_script(service_name, entrypoint, script_dir)
            service['entrypoint'] = [os.path.join('/social-network-microservices', script_path)]
    return services

def main():
    script_dir = './entrypoints'
    os.makedirs(script_dir, exist_ok=True)

    with open('docker-compose.yml', 'r') as file:
        compose_data = yaml.safe_load(file)

    services = compose_data.get('services', {})
    modified_services = process_services(services, script_dir)
    compose_data['services'] = modified_services

    with open('docker-compose-modified.yml', 'w') as file:
        yaml.safe_dump(compose_data, file)

if __name__ == "__main__":
    main()
