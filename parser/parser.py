import re
FILENAME = "test.log"

class Node():
  node_id:int = -1
  neighbours = None
  neighbour_count = 0
  
  def __init__(self):
    self.neighbours = []

class Experiment():
  TYPE:str = ""
  TRANSMISSION_WINDOW_COUNT:int
  RECEPTION_WINDOW_COUNT:int
  TRANSMISSION_WINDOW_DURATION:int
  RECEPTION_WINDOW_DURATION:int
  TRANSMISSION_PER_WINDOW:int
  TRANSMISSION_DURATION:int
  RECEPTION_DURATION:int
  
  nodes : list[Node] = []
  max_node_id = 0


def save_output(e : Experiment):
  OUT_FILENAME = f"{e.TYPE}_{e.max_node_id}.log"
  output_f = open(OUT_FILENAME, "w")

  for line in open(FILENAME, "r"):
    output_f.write(line)

def parse(filename=FILENAME):
  e = Experiment()
  e.nodes = [Node() for i in range(1,100)]
  settings_found = False

  f = open(filename, "r")
  pattern = "\d+\sID:(\d+)\sApp:\sEpoch\s(\d+)\sNew\sNBR\s(\d+)"
  # group 1: ID
  # group 2: Epoch
  # group 3: neighbour discovered

  pattern2 = "\d+\sID:(\d+)\sApp:\sEpoch\s(\d+)\sfinished\sNum\sNBR\s(\d+)"
  # group 1: ID
  # group 2: Epoch
  # group 3: num of neighbour discovered

  patter_settings = "\d+\sID:(\d+)\sSTART:\s(.+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+)"
  """
  ID
  TYPE 
  TRANSMISSION_WINDOW_COUNT
  RECEPTION_WINDOW_COUNT
  TRANSMISSION_WINDOW_DURATION
  RECEPTION_WINDOW_DURATION
  TRANSMISSION_PER_WINDOW
  TRANSMISSION_DURATION
  RECEPTION_DURATION
  """

  for line in f:
    if not settings_found:
      m = re.search(patter_settings, line)

      if m:
        e.TYPE = m.group(2)
        e.TRANSMISSION_WINDOW_COUNT = m.group(3)
        e.RECEPTION_WINDOW_COUNT = m.group(4)
        e.TRANSMISSION_WINDOW_DURATION = m.group(5)
        e.RECEPTION_WINDOW_DURATION = m.group(6)
        e.TRANSMISSION_PER_WINDOW = m.group(7)
        e.TRANSMISSION_DURATION = m.group(8)
        e.RECEPTION_DURATION = m.group(9)
        settings_found = True

    match = re.search(pattern, line)

    if match:
      node_id = int(match.group(1))
      epoch = match.group(2)
      n_discovered = int(match.group(3))
      
      if node_id > e.max_node_id:
        e.max_node_id = node_id

      n = e.nodes[int(node_id)] 
      n.node_id = node_id
      if (not n_discovered in n.neighbours):
        n.neighbours.append(n_discovered)

    else:
      finish_match = re.search(pattern2, line)
      if finish_match:
        node_id = finish_match.group(1)
        epoch = finish_match.group(2)
        n_count_discovered = int(finish_match.group(3))

        n = e.nodes[int(node_id)] 
        n.neighbour_count += n_count_discovered
        #print(f"id: {node_id}, epoch: {epoch}, n_discovered: {n_count_discovered}")

  f.close()

  for i in e.nodes:
    if i.node_id != -1:
      print(f"node id: {i.node_id}, neigh_count: {i.neighbour_count}, neighbours: {i.neighbours}")

  save_output(e)

parse()