import os

queue_size = range(1500,50000, 100)

print queue_size

for size in queue_size:
    print size
    cmd = "sed -i \"s/queue_size: [0-9]\+/queue_size: " + str(size) + "/\" conf_joao_trace.txt";
    print cmd 
    os.system(cmd)
    os.system("../../simulator 1 conf_joao_trace.txt >> output")

