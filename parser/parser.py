import re
FILENAME = "test.log"

def parse(filename=FILENAME):
  f = open(filename, "r")
  pattern = "\d+\sID:(\d+)\sApp:\sEpoch\s(\d+)\sNew\sNBR\s(\d+)"
  # group 1: ID
  # group 2: Epoch
  # group 3: neighbour discovered

  pattern2 = "\d+\sID:(\d+)\sApp:\sEpoch\s(\d+)\sfinished\sNum\sNBR\s(\d+)"
  # group 1: ID
  # group 2: Epoch
  # group 3: num of neighbour discovered

  for line in f:
    match = re.search(pattern, line)

    if match:
      node_id = match.group(1)
      epoch = match.group(2)
      n_discovered = match.group(3)
      
    else:
      finish_match = re.search(pattern2, line)
      if finish_match:
        node_id = finish_match.group(1)
        epoch = finish_match.group(2)
        n_count_discovered = finish_match.group(3)
        print(f"id: {node_id}, epoch: {epoch}, n_discovered: {n_discovered}")

parse()