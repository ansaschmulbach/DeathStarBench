req_freqs=(1000 2000 4000 7000 10000 15000 20000 30000 50000 70000 100000 150000 200000 500000 700000 1000000 10000000)
for num in "${req_freqs[@]}"; do
	latency=$(../wrk2/wrk -D exp -t 10 -c 100 -d 30 -L -s ../socialNetwork/wrk2/scripts/social-network/compose-post.lua http://10.0.1.1:8080/wrk2-api/post/compose -R $num | grep "Latency" | head -n 1 | awk '{print $2}')
	echo "Latency for $num: $latency"
done
