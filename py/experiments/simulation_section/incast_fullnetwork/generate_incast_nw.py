import random

print "Generating incast traffic workload"

N = 40
SIZE = 100
TIME = 2.0

dest = random.randint(0,143)

print "Destination is: ", dest


senders = []

while len(senders) != N:
    new = random.randint(0,143)
    if new == dest:
        continue;
    senders.append(new)

# id >> start_time >> temp >> temp >> size >> temp >> temp >> s >> d
#0 2.0 -1 -1 100 -1 -1 1 0
id_counter = 0
for sender in senders:
    id_counter = id_counter + 1
    print id_counter, " ", TIME, " ", "-1 -1 ", SIZE, " ", "-1 -1 ", sender, " ", dest
    
print senders
