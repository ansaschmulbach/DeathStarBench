import requests
import string
import random
import sys

random.seed(42)

def stringRandom(length):
    characters = string.ascii_letters + string.digits
    random_string = ''.join(random.choice(characters) for _ in range(length))
    return random_string

def gen_request():
    max_user_index = 962
    user_index = random.randint(0, 962)
    username = "username_" + str(user_index)
    user_id = str(user_index)
    text = stringRandom(256)
    num_user_mentions = random.randint(0, 5)
    num_urls = random.randint(0, 5)
    num_media = random.randint(0, 4)
    media_ids = "["
    media_types = "["

    for i in range(num_user_mentions):
        while(1):
            user_mention_id = random.randint(0, max_user_index)
            if (user_index != user_mention_id): 
                break
        text += " @username_" + str(user_mention_id)

    for i in range(num_urls):
        text += " http://" + stringRandom(64)

    for i in range(num_media):
        media_id = str(random.randint(0,10 ** 18))
        media_ids += '"' + media_id + '",'
        media_types += '"png",'

    media_ids = media_ids[:-1] + "]"
    media_types = media_types[:-1] + "]"


    path = "http://localhost:8080/wrk2-api/post/compose"
    headers = {}
    body = {}
    headers["Content-Type"] = "application/x-www-form-urlencoded"
    body["username"] = username
    body["user_id"] = user_id
    body["text"] = text
    body["post_type"] = 0
    if num_media:
        body["media_ids"] = media_ids
        body["media_types"] = media_types
    else:
        body["media_ids"] = ""

    return path, headers, body

if len(sys.argv) != 2:
    print("error: must supply number of requests")
    exit()

req_num = int(sys.argv[1])

for i in range(req_num):
    path, headers, body = gen_request()
    response = requests.post(path, data=body, headers=headers)
    print(response.status_code)
    print(response.text)
