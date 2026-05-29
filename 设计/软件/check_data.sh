#!/bin/bash
S1=$(curl -s --noproxy "*" http://127.0.0.1:8090/api/summary | python3 -c "import json,sys;d=json.load(sys.stdin);print(d['serial_tx_bytes'],d.get('robot_yaw',0))")
sleep 10
S2=$(curl -s --noproxy "*" http://127.0.0.1:8090/api/summary | python3 -c "import json,sys;d=json.load(sys.stdin);print(d['serial_tx_bytes'],d.get('robot_yaw',0))")
echo "10s delta: $S1 -> $S2"
