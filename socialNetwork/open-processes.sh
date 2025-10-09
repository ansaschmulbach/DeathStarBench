rm -rf /data/sanchez/users/ansa/uds/dsb-sock-*
/data/sanchez/users/ansa/DSB2/socialNetwork/cmake-build/src/ComposePostService/ComposePostService &
/data/sanchez/users/ansa/DSB2/socialNetwork/cmake-build/src/HomeTimelineService/HomeTimelineService &  
/data/sanchez/users/ansa/DSB2/socialNetwork/cmake-build/src/PostStorageService/PostStorageService &
/data/sanchez/users/ansa/DSB2/socialNetwork/cmake-build/src/SocialGraphService/SocialGraphService &
# /data/sanchez/users/ansa/DSB2/socialNetwork/cmake-build/src/TextService/TextService &
/data/sanchez/users/ansa/DSB2/socialNetwork/cmake-build/src/UniqueIdService/UniqueIdService &
/data/sanchez/users/ansa/DSB2/socialNetwork/cmake-build/src/UrlShortenService/UrlShortenService &
/data/sanchez/users/ansa/DSB2/socialNetwork/cmake-build/src/UserMentionService/UserMentionService &
/data/sanchez/users/ansa/DSB2/socialNetwork/cmake-build/src/UserService/UserService &
/data/sanchez/users/ansa/DSB2/socialNetwork/cmake-build/src/UserTimelineService/UserTimelineService &
/data/sanchez/users/ansa/DSB2/socialNetwork/cmake-build/src/MediaService/MediaService &

sleep 1

chmod 777 /data/sanchez/users/ansa/uds/dsb-sock-393*
docker restart socialnetwork-nginx-thrift-1
