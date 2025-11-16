import re
import matplotlib.pyplot as plt

def parse_emusync_logs(filename):
    data = {}
    current_name = None
    expected_items = 0
    collected = 0
    time_list = []
    value_list = []

    header_re = re.compile(r"^EMUSYNC LOG \((\d+) items\): (.+)$")

    with open(filename, "r") as f:
        for line in f:
            line = line.strip()

            # Detect header lines
            match = header_re.match(line)
            if match:
                # If we were collecting a previous block, store it
                if current_name is not None:
                    data[current_name] = {
                        "time": time_list,
                        "value": value_list
                    }

                # Start new block
                expected_items = int(match.group(1))
                current_name = match.group(2)
                collected = 0
                time_list = []
                value_list = []
                continue

            # Collect data lines: "<float>,<float>"
            if current_name is not None and collected < expected_items:
                if "," in line:
                    try:
                        t, v = line.split(",")
                        t = float(t)
                        v = float(v)
                        time_list.append(t)
                        value_list.append(v)
                        collected += 1
                    except ValueError:
                        pass

    # Store last block
    if current_name is not None:
        data[current_name] = {
            "time": time_list,
            "value": value_list
        }

    return data


def plot_emusync_data(data):
    num_plots = len(data)
    fig, axes = plt.subplots(num_plots, 1, figsize=(10, 3 * num_plots), sharex=False)

    # If only 1 subplot, axes is not a list
    if num_plots == 1:
        axes = [axes]

    for ax, (name, block) in zip(axes, data.items()):
        ax.plot(block["time"], block["value"], marker="o", markersize=3)
        ax.set_title(name)
        ax.set_xlabel("Time")
        ax.set_ylabel("Value")
        ax.grid(True)

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    log_file = "log.txt"   # ← change to your filename
    parsed = parse_emusync_logs(log_file)
    plot_emusync_data(parsed)
