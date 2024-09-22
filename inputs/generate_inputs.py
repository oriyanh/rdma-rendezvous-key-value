import random
import string


GET_REQUEST = "GET"
SET_REQUEST = "SET"

def generate_random_string(length):
    if length == 0:
        return ""

    letter_set = string.digits + string.ascii_letters
    permutation = "".join(random.choice(letter_set) for _ in range(length))
    return permutation

def generate_get_request(key):
    return f"GET,{len(key)},{key}\n"

def generate_set_request(key, length):
    request_str = f"SET,{len(key)},{key},"
    value = generate_random_string(length)
    assert(length == len(value))

    request_str += f"{length},{value}\n"
    return request_str


def generate_same_key_short(filename, iters):
    header_len = 24
    buffer_len = 4096
    keylen = 10
    key = generate_random_string(keylen)
    # Only eager set and (sometimes) eager get
    value_len_max = buffer_len - header_len - keylen - 2  # Include NULL characters
    with open(filename, "w") as request_file:
        for i in range(iters):
            value_len = random.randint(0, value_len_max)
            set_request = generate_set_request(key, value_len)
            request_file.write(set_request)
            request_file.flush()
            get_request = generate_get_request(key)
            request_file.write(get_request)
            request_file.flush()

def generate_same_key(filename, iters):
    with open(filename, "w") as request_file:
        keylen = random.randint(1, 10)
        key = generate_random_string(keylen)
        for i in range(iters):
            print(i)
            value_len = random.randint(0, 1 << 22)
            set_request = generate_set_request(key, value_len)
            request_file.write(set_request)
            request_file.flush()
            get_request = generate_get_request(key)
            request_file.write(get_request)
            request_file.flush()

def generate_random_key(filename, iters):
    with open(filename, "w") as request_file:
        for i in range(iters):
            keylen = random.randint(1, 4070)
            key = generate_random_string(keylen)
            value_len = random.randint(0, 1 << 22)
            set_request = generate_set_request(key, value_len)
            request_file.write(set_request)
            request_file.flush()
            get_request = generate_get_request(key)
            request_file.write(get_request)
            request_file.flush()

def generate_long_value(filename, iters):
    with open(filename, "w") as request_file:
        for i in range(iters):
            keylen = 10
            key = generate_random_string(keylen)
            value_len = random.randint(1 << 23, 1 << 24)
            set_request = generate_set_request(key, value_len)
            request_file.write(set_request)
            request_file.flush()
            get_request = generate_get_request(key)
            request_file.write(get_request)
            request_file.flush()
        

def generate_longer_value(filename, iters):
    with open(filename, "w") as request_file:
        for i in range(iters):
            keylen = 10
            key = generate_random_string(keylen)
            value_len = random.randint(1 << 25, 1 << 26)
            set_request = generate_set_request(key, value_len)
            request_file.write(set_request)
            request_file.flush()
            get_request = generate_get_request(key)
            request_file.write(get_request)
            request_file.flush()


def generate_even_longer_value(filename, iters):
    with open(filename, "w") as request_file:
        for i in range(iters):
            keylen = 10
            key = generate_random_string(keylen)
            value_len = random.randint(1 << 26, 1 << 27)
            set_request = generate_set_request(key, value_len)
            request_file.write(set_request)
            request_file.flush()
            get_request = generate_get_request(key)
            request_file.write(get_request)
            request_file.flush()


def generate_1gb_value(filename, iters):
    total_len = 0
    batch_len = 1 << 22
    
    with open(filename, "w") as request_file:
        for i in range(iters):
            keylen = 10
            key = generate_random_string(keylen)
            value_len = random.randint(1 << 29, 1 << 30 - 1)
            request_file.write(f"SET,{keylen},{key},{value_len},")
            while total_len < value_len:
                leftover = value_len - total_len
                curr_batch_len = min(leftover, batch_len)
                random_str = generate_random_string(curr_batch_len)
                total_len += (curr_batch_len)
                request_file.write(random_str)
                request_file.flush()
            # line = f"SET,{keylen},{key},{value_len},"
            # set_request = generate_set_request(key, value_len)
            request_file.write('\n')
            request_file.flush()
            get_request = generate_get_request(key)
            request_file.write(get_request)
            request_file.flush()
            total_len = 0

def main():
    iters = 1000
    generate_same_key("same_key.txt", 50)
    # generate_same_key_short("same_key_short.txt", 50000)
    # generate_random_key("random_key.txt", 1)
    # generate_long_value("long_value.txt", 1)
    # generate_longer_value("longer_value.txt", 1)
    # generate_even_longer_value("even_longer_value.txt", 1)
    # generate_1gb_value("1GB.txt", 1)

if __name__ == "__main__":
    main()

