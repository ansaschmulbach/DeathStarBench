req_freqs=(600 800 900 1000 1100 1200 1300 1400 1500)
for num in "${req_freqs[@]}"; do
	latency=$(../wrk2/wrk -D exp -t 10 -c 100 -d 30 -L -s ../socialNetwork/wrk2/scripts/social-network/compose-post.lua http://10.0.1.1:8080/wrk2-api/post/compose -R $num | grep "Latency" | head -n 1 | awk '{print $2}')
	echo "Latency for $num: $latency"
done
