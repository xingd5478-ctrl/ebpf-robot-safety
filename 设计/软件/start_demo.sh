#!/bin/bash
cd /mnt/c/Users/xing2/Desktop/ebpf-robot-safety/ebpf-robot-safety/设计/软件
python3 ros2/demo_control.py --fault 0 > /tmp/demo.log 2>&1 &
echo "demo PID: $!"
