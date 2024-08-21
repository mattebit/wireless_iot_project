import math
import re
from datetime import datetime
from os.path import join

import matplotlib.pyplot as plt
import numpy as np

FILENAME = "test.log"
LOGS_FOLDER = "logs"

# Select cooja or testbed files
if False:
    FILENAMES = [
        "cooja_BURST_2.log",
        "cooja_BURST_5.log",
        "cooja_BURST_10.log",
        "cooja_BURST_20.log",
        "cooja_BURST_30.log",
        "cooja_BURST_50.log",
        "cooja_SCATTER_2.log",
        "cooja_SCATTER_5.log",
        "cooja_SCATTER_10.log",
        "cooja_SCATTER_20.log",
        "cooja_SCATTER_30.log",
        "cooja_SCATTER_50.log",
    ]
else:
    FILENAMES = [
        "testbed_BURST_1-7.log",
        "testbed_SCATTER_1-7.log"
    ]


class Node:
    node_id: int = -1
    neighbours = None
    neighbour_count = 0
    duty_cycle = 0

    n_count_epoch: list  # n discovered each epoch
    n_new_count_epoch: list  # n new neighbour discovered each epoch

    def __init__(self):
        self.n_count_epoch = [0 for i in range(1000)]
        self.n_new_count_epoch = [0 for i in range(1000)]
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
    name: str

    epochs: list

    TRUNC_EP_LOW = 0
    TRUNC_EP_HIGH = 100

    nodes: list[Node]
    max_node_id = 0
    max_epoch = 0
    data_energest: dict

    dc_mean: float

    is_testbed = False
    testbed_job_id = 0

    def __init__(self):
        self.nodes = []
        self.data_energest = {}

    def clear_empty_nodes(self):
        self.nodes.pop(0)  # first element is always absent

        tmp = self.nodes.copy()

        self.nodes = []

        for i in tmp:
            if i.node_id != -1:
                self.nodes.append(i)

        # self.nodes = self.nodes[0:i]

        for n in self.nodes:
            n.neighbour_count = len(n.neighbours)

        for n in self.nodes:
            try:
                while len(n.n_count_epoch) != self.max_epoch:
                    n.n_count_epoch.pop(-1)

                while len(n.n_new_count_epoch) != self.max_epoch:
                    n.n_new_count_epoch.pop(-1)
            except IndexError:
                pass

            n.n_count_epoch = n.n_count_epoch[self.TRUNC_EP_LOW:self.TRUNC_EP_HIGH]
            n.n_new_count_epoch = n.n_new_count_epoch[self.TRUNC_EP_LOW:self.TRUNC_EP_HIGH]

        self.epochs = [i for i in range(self.TRUNC_EP_LOW, self.TRUNC_EP_HIGH)]

    def calculate_values(self):

        arrays = []
        arrays_new = []
        for n in self.nodes:
            arrays.append(n.n_count_epoch)
            arrays_new.append(n.n_new_count_epoch)

        self.avg_n_count_epoch = np.mean(arrays, axis=0)
        self.avg_n_count_epoch_norm = self.avg_n_count_epoch / (self.max_node_id - 1)

        self.avg_n_count_epoch_overall = np.mean(arrays, axis=None)
        self.avg_n_count_epoch_overall_percentage = self.avg_n_count_epoch_overall / (self.max_node_id - 1)

        self.avg_n_new_count_epoch = np.mean(arrays_new, axis=0)
        self.avg_n_new_count_epoch_norm = self.avg_n_new_count_epoch / (self.max_node_id - 1)

        self.name = f"{"cooja" if not self.is_testbed else "testbed"}_{self.TYPE}_{self.max_node_id}"

    def calculate_energest(self):
        dc_lst = []

        ordered_keys = sorted(self.data_energest.keys())

        for nid in ordered_keys:
            v = self.data_energest[nid]

            total_time = v['cpu'] + v['lpm']
            total_radio = v['tx'] + v['rx']

            dc = 100 * total_radio / total_time
            dc_lst.append(dc)
            self.nodes[nid - 1].duty_cycle = dc

            print("Node {}:  Duty Cycle: {:.3f}%".format(nid, dc))

        self.dc_mean = sum(dc_lst) / len(dc_lst)
        dc_min = min(dc_lst)
        dc_max = max(dc_lst)
        dc_std = math.sqrt(sum([(v - self.dc_mean) ** 2 for v in dc_lst]) / len(dc_lst))

        print("\n----- Duty Cycle Overall Statistics -----\n")
        print("Average Duty Cycle: {:.3f}%\nStandard Deviation: {:.3f}\n"
              "Minimum: {:.3f}%\nMaximum: {:.3f}%\n".format(self.dc_mean,
                                                            dc_std, dc_min,
                                                            dc_max))


def save_output(e: Experiment):
    OUT_FILENAME = "testbed_" if e.is_testbed else "cooja_"
    OUT_FILENAME += f"{e.TYPE}_{e.max_node_id}.log"
    output_f = open(OUT_FILENAME, "w")

    print(f"Saved to {OUT_FILENAME}")

    for line in open(FILENAME, "r"):
        output_f.write(line)


def parse(filename=FILENAME, log_file=False, printinfo=False) -> Experiment:
    e = Experiment()
    e.nodes = [Node() for i in range(1, 1000)]
    settings_found = False

    f = open(filename, "r")

    pattern_check_testbed = "INFO:testbed-run:\sStart\stest\s(\d+)"

    # Cooja patterns
    cooja_patter_settings = "\d+\sID:(\d+)\sSTART:\s(.+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+)"
    cooja_pattern_new_n = "\d+\sID:(\d+)\sApp:\sEpoch\s(\d+)\sNew\sNBR\s(\d+)"
    cooja_pattern_epoch_end = "\d+\sID:(\d+)\sApp:\sEpoch\s(\d+)\sfinished\sNum\sNBR\s(\d+)\sNum\snew\sNBR\s(\d+)"
    record_pattern = r"(?P<time>[\w:.]+)\s+ID:(?P<self_id>\d+)\s+"
    cooja_regex_dc = re.compile(r"{}Energest: (?P<cnt>\d+) (?P<cpu>\d+) "
                                r"(?P<lpm>\d+) (?P<tx>\d+) (?P<rx>\d+)".format(record_pattern))

    # Testbed patterns
    testbed_pattern_settings = "INFO:firefly.(\d+):\s\d+.firefly\s<\sb'START:\s(.+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+),\s(\d+)"
    testbed_pattern_new_n = "INFO:firefly.(\d+):\s\d+.firefly\s<\sb'App:\sEpoch\s(\d+)\sNew\sNBR\s(\d+)"
    testbed_pattern_epoch_end = "INFO:firefly.(\d+):\s\d+.firefly\s<\sb'App:\sEpoch\s(\d+)\sfinished\sNum\sNBR\s(\d+)\sNum\snew\sNBR\s(\d+)"
    testbed_record_pattern = r"\[(?P<time>.{23})\] INFO:firefly\.(?P<self_id>\d+): \d+\.firefly < b"
    testbed_regex_dc = re.compile(r"{}'Energest: (?P<cnt>\d+) (?P<cpu>\d+) "
                                  r"(?P<lpm>\d+) (?P<tx>\d+) (?P<rx>\d+)'".format(testbed_record_pattern))

    pattern_settings = cooja_patter_settings
    pattern_new_n = cooja_pattern_new_n
    pattern_epoch_end = cooja_pattern_epoch_end
    regex_dc = cooja_regex_dc

    c = 0
    for line in f:
        if c == 0:
            m = re.search(pattern_check_testbed, line)
            if m:
                e.is_testbed = True
                e.testbed_job_id = int(m.group(1))
                pattern_settings = testbed_pattern_settings
                pattern_new_n = testbed_pattern_new_n
                pattern_epoch_end = testbed_pattern_epoch_end
                regex_dc = testbed_regex_dc

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

        if epoch <= 199:
            if match:
                node_id = int(match.group(1))
                epoch = int(match.group(2))
                n_discovered = int(match.group(3))

                if node_id > e.max_node_id:
                    e.max_node_id = node_id

                print(node_id)
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
                        continue

                    n_count_discovered = int(finish_match.group(3))
                    n_count_discovered_new = int(finish_match.group(4))  # new discovered

                    n = e.nodes[node_id]
                    n.neighbour_count += n_count_discovered_new
                    n.n_count_epoch[epoch] = n_count_discovered
                    n.n_new_count_epoch[epoch] = n_count_discovered_new
                    # print(f"id: {node_id}, epoch: {epoch}, n_discovered: {n_count_discovered}")

        # Energ test

        m = regex_dc.match(line)
        if m:
            d = m.groupdict()
            if e.is_testbed:
                ts = datetime.strptime(d["time"], '%Y-%m-%d %H:%M:%S,%f')
                ts = ts.timestamp()
            else:
                ts = d["time"]

            d['self_id'] = int(d['self_id'])
            d['cpu'] = int(d['cpu'])
            d['lpm'] = int(d['lpm'])
            d['tx'] = int(d['tx'])
            d['rx'] = int(d['rx'])

            if e.data_energest.get(d['self_id']) is None:
                e.data_energest[d['self_id']] = {
                    'self_id': d['self_id'],
                    'cpu': d['cpu'],
                    'lpm': d['lpm'],
                    'tx': d['tx'],
                    'rx': d['rx'],
                }

            if int(d['cnt']) >= 2:
                e.data_energest[d['self_id']]['cpu'] += d['cpu']
                e.data_energest[d['self_id']]['lpm'] += d['lpm']
                e.data_energest[d['self_id']]['tx'] += d['tx']
                e.data_energest[d['self_id']]['rx'] += d['rx']

        c += 1

        e.max_epoch = max(e.max_epoch, epoch)

    e.clear_empty_nodes()
    e.calculate_energest()
    e.calculate_values()

    f.close()

    if printinfo:
        print_output(e)

    if log_file:
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

    for n in e.nodes:
        axs.plot(e.epochs, n.n_count_epoch, label=n.node_id)
        # axs[0].set_xlim(0, 2)
        axs.set_xlabel('Epoch')
        axs.set_ylabel('s1 and s2')
        # axs.grid(True)

    # plt.legend()
    plt.show()


def plot_n_discovered_per_epoch_avg(e: Experiment):
    fig, axs = plt.subplots(1, 1)

    axs.plot(e.epochs, e.avg_n_count_epoch, label="n discovered avg")
    # axs[0].set_xlim(0, 2)
    axs.set_xlabel('Epoch')
    axs.set_ylabel('s1 and s2')
    # axs.grid(True)

    # plt.legend()
    plt.show()


def plot_n_new_discovered_per_epoch(e: Experiment):
    fig, axs = plt.subplots(1, 1)

    for n in e.nodes:
        axs.plot(e.epochs, n.n_new_count_epoch, label=n.node_id)
        axs.set_xlabel('Epoch')
        axs.set_ylabel('s1 and s2')

    # plt.legend()
    plt.show()


def get_min_epoch(exps: list[Experiment]) -> int:
    min_epoch = 999
    for expm in exps:
        if expm.TRUNC_EP_HIGH != 0:
            return expm.TRUNC_EP_HIGH

        else:
            min_epoch = min(min_epoch, expm.max_epoch)

    return min_epoch


# TODO USE
def plot_exps_n_discovered_per_epoch(exps: list[Experiment]):
    fig, axs = plt.subplots(1, 1)

    min_epoch = get_min_epoch(exps)

    for expm in exps:
        # print(f"{len(expm.epochs)} {len(expm.avg_n_count_epoch_norm)}")
        axs.plot(expm.epochs, expm.avg_n_count_epoch_norm[:min_epoch], label=expm.name)
        axs.set_xlabel('Epoch')
        axs.set_ylabel('average node discovered / all nodes in network')
        print(f"{expm.name}: Avg n discovered overall norm {expm.avg_n_count_epoch_overall_percentage}")

    plt.legend()
    plt.show()


# TODO: USE
def plot_exps_n_new_discovered_per_epoch(exps: list[Experiment]):
    fig, axs = plt.subplots(1, 1)

    min_epoch = get_min_epoch(exps)

    for expm in exps:
        axs.plot(expm.epochs[:12], expm.avg_n_new_count_epoch_norm[:12], label=expm.name)
        axs.set_xlabel('Epoch')
        axs.set_ylabel('average new nodes discovered / all nodes')

    plt.legend()
    plt.show()


# TODO: usare
def plot_exps_dc_avg_n_discovered(exps: list[Experiment]):
    fig, ax = plt.subplots()

    for e in exps:
        ax.scatter(e.avg_n_count_epoch_overall_percentage, e.dc_mean, label=e.name)

    ax.set_xlabel('average node discovered / all nodes')
    ax.set_ylabel('average duty cycle (%)')

    plt.legend()
    plt.show()


def run_exps():
    FILEPATHS = []

    for fn in FILENAMES:
        FILEPATHS.append(join(LOGS_FOLDER, fn))

    expmnts = []

    for fp in FILEPATHS:
        expmnts.append(parse(fp))

    print("Experiments considered:")
    for ep in expmnts:
        print(f"Exp: {ep.TYPE} {ep.max_node_id}")
        print(f"\tAvg dc: {ep.dc_mean}")

    # plot_exps_n_new_discovered_per_epoch(expmnts)
    # plot_exps_dc_avg_n_discovered(expmnts)
    plot_exps_n_discovered_per_epoch(expmnts)

    # for ep in expmnts:
    #   plot_n_discovered_per_epoch_avg(ep)

    """
    exit()
    e: Experiment = parse()
    
    e.calculate_values()
    print(e.avg_n_count_epoch_overall)
    
    #plot_n_discovered_per_epoch_avg(e)
    """


run_exps()  # Generate graphs
# parse(log_file=True, printinfo=True) # First file parse
