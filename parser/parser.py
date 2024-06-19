import re

import matplotlib.pyplot as plt
import numpy as np

FILENAME = "test.log"

FILENAMES = [
    "BURST_2.log",
    "BURST_5.log",
    "BURST_10.log",
    "BURST_20.log",
    "BURST_30.log",
    "BURST_50.log",
    "SCATTER_2.log",
    "SCATTER_5.log",
    "SCATTER_10.log",
    "SCATTER_20.log",
    "SCATTER_30.log",
    "SCATTER_50.log",
]


class Node:
    node_id: int = -1
    neighbours = None
    neighbour_count = 0

    n_count_epoch: list  # n discovered each epoch
    n_new_count_epoch: list  # n new neighbour discovered each epoch

    def __init__(self):
        self.n_count_epoch = [0 for i in range(200)]
        self.n_new_count_epoch = [0 for i in range(200)]
        self.neighbours = []


class Experiment:
    TYPE: str = ""
    TRANSMISSION_WINDOW_COUNT: int
    RECEPTION_WINDOW_COUNT: int
    TRANSMISSION_WINDOW_DURATION: int
    RECEPTION_WINDOW_DURATION: int
    TRANSMISSION_PER_WINDOW: int
    TRANSMISSION_DURATION: int
    RECEPTION_DURATION: int

    nodes: list[Node] = []
    max_node_id = 0
    max_epoch = 0

    is_testbed = False
    testbed_job_id = 0

    def clear_empty_nodes(self):
        self.nodes.pop(0)  # first element is always absent

        for i, n in enumerate(self.nodes):
            if n.node_id == -1:
                break

        self.nodes = self.nodes[0:i]

        for n in self.nodes:
            n.neighbour_count = len(n.neighbours)

    def calculate_values(self):

        arrays = []
        for n in self.nodes:
            arrays.append(n.n_count_epoch)

        self.avg_n_count_epoch = np.mean(arrays, axis=0)
        self.avg_n_count_epoch_overall = np.mean(arrays, axis=None)


def save_output(e: Experiment):
    OUT_FILENAME = "testbed_" if e.is_testbed else "cooja_"
    OUT_FILENAME += f"{e.TYPE}_{e.max_node_id}.log"
    output_f = open(OUT_FILENAME, "w")

    for line in open(FILENAME, "r"):
        output_f.write(line)


def parse(filename=FILENAME) -> Experiment:
    e = Experiment()
    e.nodes = [Node() for i in range(1, 100)]
    settings_found = False

    f = open(filename, "r")

    pattern_check_testbed = "INFO:testbed-run:\sStart\stest\s(\d+)"

    # Cooja patterns
    cooja_patter_settings = "\d+\sID:(\d+)\sSTART:\s(.+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+)"
    cooja_pattern_new_n = "\d+\sID:(\d+)\sApp:\sEpoch\s(\d+)\sNew\sNBR\s(\d+)"
    cooja_pattern_epoch_end = "\d+\sID:(\d+)\sApp:\sEpoch\s(\d+)\sfinished\sNum\sNBR\s(\d+)\sNum\snew\sNBR\s(\d+)"

    # Testbed patterns
    testbed_pattern_settings = "INFO:firefly.(\d+):\s\d+.firefly\s<\sb'START:\s(.+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+)"
    testbed_pattern_new_n = "INFO:firefly.(\d+):\s\d+.firefly\s<\sb'App:\sEpoch\s(\d+)\sNew\sNBR\s(\d+)"
    testbed_pattern_epoch_end = "INFO:firefly.(\d+):\s\d+.firefly\s<\sb'App:\sEpoch\s(\d+)\sfinished\sNum\sNBR\s(\d+)\sNum\snew\sNBR\s(\d+)"

    pattern_settings = cooja_patter_settings
    pattern_new_n = cooja_pattern_new_n
    pattern_epoch_end = cooja_pattern_epoch_end

    c = 0
    for line in f:
        if c==0:
            m = re.search(pattern_check_testbed, line)
            if m:
                e.is_testbed = True
                e.testbed_job_id = int(m.group(1))
                pattern_settings = testbed_pattern_settings
                pattern_new_n = testbed_pattern_new_n
                pattern_epoch_end = testbed_pattern_epoch_end

        if not settings_found:
            m = re.search(pattern_settings, line)

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

        match = re.search(pattern_new_n, line)

        epoch = 0

        if match:
            node_id = int(match.group(1))
            epoch = int(match.group(2))
            n_discovered = int(match.group(3))

            if node_id > e.max_node_id:
                e.max_node_id = node_id

            n = e.nodes[int(node_id)]
            n.node_id = node_id
            if (not n_discovered in n.neighbours):
                n.neighbours.append(n_discovered)

        else:
            finish_match = re.search(pattern_epoch_end, line)
            if finish_match:
                node_id = int(finish_match.group(1))
                epoch = int(finish_match.group(2))
                if epoch > 199:
                    break

                n_count_discovered = int(finish_match.group(3))
                n_count_discovered_new = int(finish_match.group(4))  # new discovered

                n = e.nodes[node_id]
                n.neighbour_count += n_count_discovered_new
                n.n_count_epoch[epoch] = n_count_discovered
                n.n_new_count_epoch[epoch] = n_count_discovered_new
                # print(f"id: {node_id}, epoch: {epoch}, n_discovered: {n_count_discovered}")

            else:
                pass
                # print(line)

            c += 1

        e.max_epoch = max(e.max_epoch, epoch)

    e.clear_empty_nodes()

    for n in e.nodes:
        while len(n.n_count_epoch) != e.max_epoch:
            n.n_count_epoch.pop(-1)

        while len(n.n_new_count_epoch) != e.max_epoch:
            n.n_new_count_epoch.pop(-1)

    f.close()
    print_output(e)
    save_output(e)

    return e


def print_output(e: Experiment):
    n_perc_sum = 0

    for i in e.nodes:
        print(f"max n id {e.max_node_id}")
        discover_perc = round(i.neighbour_count / (e.max_node_id - 1), 2)
        n_perc_sum += discover_perc
        print(
            f"node id: {i.node_id},\tneigh_count: {i.neighbour_count}/{e.max_node_id - 1},\tdiscover_perc: {discover_perc},\tneighbours: {i.neighbours}")
        print(f"neigh: {i.n_count_epoch}")
        print(f"neigh new: {i.n_new_count_epoch}")
    print(f"avg_discover_perc: {n_perc_sum / (e.max_node_id)}")


def plot_n_discovered_per_epoch(e: Experiment):
    fig, axs = plt.subplots(1, 1)

    ep = [i for i in range(e.max_epoch)]

    for n in e.nodes:
        axs.plot(ep, n.n_count_epoch, label=n.node_id)
        # axs[0].set_xlim(0, 2)
        axs.set_xlabel('Epoch')
        axs.set_ylabel('s1 and s2')
        # axs.grid(True)

    # plt.legend()
    plt.show()


def plot_n_discovered_per_epoch_avg(e: Experiment):
    fig, axs = plt.subplots(1, 1)

    ep = [i for i in range(e.max_epoch)]

    axs.plot(ep, e.avg_n_count_epoch, label="n discovered avg")
    # axs[0].set_xlim(0, 2)
    axs.set_xlabel('Epoch')
    axs.set_ylabel('s1 and s2')
    # axs.grid(True)

    # plt.legend()
    plt.show()


def plot_n_new_discovered_per_epoch(e: Experiment):
    fig, axs = plt.subplots(1, 1)

    ep = [i for i in range(e.max_epoch)]

    for n in e.nodes:
        axs.plot(ep, n.n_new_count_epoch, label=n.node_id)
        axs.set_xlabel('Epoch')
        axs.set_ylabel('s1 and s2')

    # plt.legend()
    plt.show()


e: Experiment = parse()

e.calculate_values()
print(e.avg_n_count_epoch_overall)

plot_n_new_discovered_per_epoch(e)
